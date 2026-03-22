#include "http_agent.h"

#include <nlohmann/json.hpp>
#include <set>

#include "core/logger.h"
#include "core/subscriber.h"
#include "core/uuid.h"

namespace agui {

// Builder Implementation

HttpAgent::Builder::Builder() : m_timeout(30) {}

HttpAgent::Builder& HttpAgent::Builder::withUrl(const std::string& url) {
    m_url = url;
    return *this;
}

HttpAgent::Builder& HttpAgent::Builder::withHeader(const std::string& name, const std::string& value) {
    m_headers[name] = value;
    return *this;
}

HttpAgent::Builder& HttpAgent::Builder::withBearerToken(const std::string& token) {
    m_headers["Authorization"] = "Bearer " + token;
    return *this;
}

HttpAgent::Builder& HttpAgent::Builder::withTimeout(uint32_t seconds) {
    m_timeout = seconds;
    return *this;
}

HttpAgent::Builder& HttpAgent::Builder::withAgentId(const AgentId& id) {
    m_agentId = id;
    return *this;
}

HttpAgent::Builder& HttpAgent::Builder::withInitialMessages(const std::vector<Message>& messages) {
    m_initialMessages = messages;
    return *this;
}

HttpAgent::Builder& HttpAgent::Builder::withInitialState(const nlohmann::json& state) {
    m_initialState = state.dump();
    return *this;
}

std::unique_ptr<HttpAgent> HttpAgent::Builder::build() {
    if (m_url.empty()) {
        throw AgentError(ErrorType::Validation, ErrorCode::ValidationError, "Base URL is required");
    }

    // Set default Content-Type
    if (m_headers.find("Content-Type") == m_headers.end()) {
        m_headers["Content-Type"] = "application/json";
    }

    return std::unique_ptr<HttpAgent>(new HttpAgent(m_url, m_headers, m_agentId, m_initialMessages, m_initialState, m_timeout));
}

HttpAgent::Builder HttpAgent::builder() {
    return Builder();
}

// HttpAgent Implementation

HttpAgent::HttpAgent(const std::string& baseUrl, const std::map<std::string, std::string>& headers,
                     const AgentId& agentId, const std::vector<Message>& initialMessages,
                     const std::string& initialState, uint32_t timeoutSeconds)
    : m_baseUrl(baseUrl), m_headers(headers), m_agentId(agentId), m_timeoutSeconds(timeoutSeconds) {
    m_httpService = std::unique_ptr<HttpService>(new HttpService());
    m_sseParser = std::unique_ptr<SseParser>(new SseParser());

    // Create persistent EventHandler
    m_eventHandler = std::make_shared<EventHandler>(initialMessages, initialState,
                                                    std::vector<std::shared_ptr<IAgentSubscriber>>());

    Logger::infof("HttpAgent created with ", initialMessages.size(), " initial messages");
}

HttpAgent::~HttpAgent() {}

AgentId HttpAgent::agentId() const {
    return m_agentId;
}

// State access (delegated to EventHandler)

const std::vector<Message>& HttpAgent::messages() const {
    return m_eventHandler->messages();
}

const std::string& HttpAgent::state() const {
    return m_eventHandler->state();
}

// State modification (delegated to EventHandler)

void HttpAgent::addMessage(const Message& message) {
    // Get current messages and add new message
    auto msgs = m_eventHandler->messages();
    msgs.push_back(message);

    // Create and apply mutation
    AgentStateMutation mutation;
    mutation.withMessages(msgs);
    m_eventHandler->applyMutation(mutation);

    Logger::infof("Message added, total messages: ", msgs.size());
}

void HttpAgent::setMessages(const std::vector<Message>& messages) {
    AgentStateMutation mutation;
    mutation.withMessages(messages);
    m_eventHandler->applyMutation(mutation);

    Logger::infof("Messages set, total messages: ", messages.size());
}

void HttpAgent::setState(const nlohmann::json& state) {
    AgentStateMutation mutation;
    mutation.withState(state);
    m_eventHandler->applyMutation(mutation);

    Logger::info("State updated");
}

// Subscriber management (delegated to EventHandler)

void HttpAgent::subscribe(std::shared_ptr<IAgentSubscriber> subscriber) {
    m_eventHandler->addSubscriber(subscriber);
    Logger::info("Subscriber added");
}

void HttpAgent::unsubscribe(std::shared_ptr<IAgentSubscriber> subscriber) {
    m_eventHandler->removeSubscriber(subscriber);
    Logger::info("Subscriber removed");
}

void HttpAgent::clearSubscribers() {
    m_eventHandler->clearSubscribers();
    Logger::info("All subscribers cleared");
}

// Middleware management

HttpAgent& HttpAgent::use(std::shared_ptr<IMiddleware> middleware) {
    m_middlewareChain.addMiddleware(middleware);
    Logger::infof("Middleware added, total: ", m_middlewareChain.size());
    return *this;
}

MiddlewareChain& HttpAgent::middlewareChain() {
    return m_middlewareChain;
}

void HttpAgent::setHttpService(std::unique_ptr<IHttpService> service) {
    m_httpService = std::move(service);
}

// runAgent implementation

void HttpAgent::runAgent(const RunAgentParams& params, AgentSuccessCallback onSuccess, AgentErrorCallback onError) {
    Logger::info("Starting agent run");

    m_runErrorOccurred = false;
    m_runErrorMessage.clear();

    // Snapshot message IDs present before this run so we can compute the delta later
    m_preRunMessageIds.clear();
    for (const auto& msg : m_eventHandler->messages()) {
        m_preRunMessageIds.insert(msg.id());
    }

    // Reset result from previous run
    m_eventHandler->clearResult();

    // Clear SSE parser for new request
    m_sseParser->clear();

    // 1. Build RunAgentInput with current messages and state
    RunAgentInput input;
    input.threadId = params.threadId.empty() ? UuidGenerator::generate() : params.threadId;
    input.runId = params.runId.empty() ? UuidGenerator::generate() : params.runId;
    // params.state overrides EventHandler state if provided; otherwise use persistent state
    input.state = params.state.empty() ? m_eventHandler->state() : params.state;
    // Start with persistent message history, then append any per-call messages from params
    input.messages = m_eventHandler->messages();
    for (const auto& msg : params.messages) {
        input.messages.push_back(msg);
    }
    input.tools = params.tools;
    input.context = params.context;
    input.forwardedProps = params.forwardedProps;

    Logger::debugf("Thread ID: ", input.threadId);
    Logger::debugf("Run ID: ", input.runId);
    Logger::debugf("Messages count: ", input.messages.size());

    // 2. Process request through middleware
    MiddlewareContext middlewareContext(&input, nullptr);
    middlewareContext.currentMessages = &m_eventHandler->messages();
    middlewareContext.currentState = &m_eventHandler->state();
    
    if (!m_middlewareChain.empty()) {
        Logger::infof("Processing request through ", m_middlewareChain.size(), " middlewares");
        input = m_middlewareChain.processRequest(input, middlewareContext);
        
        // Check if should continue
        if (!middlewareContext.shouldContinue) {
            Logger::errorf("Middleware stopped execution");
            if (onError) {
                onError("Middleware stopped execution");
            }
            return;
        }
    }

    // 3. Add per-run subscribers to EventHandler (tracked for cleanup after run)
    m_perRunSubscribers = params.subscribers;
    for (auto& subscriber : m_perRunSubscribers) {
        m_eventHandler->addSubscriber(subscriber);
    }
    Logger::debugf("Per-run subscribers added: ", m_perRunSubscribers.size());

    // 4. Build HTTP request
    HttpRequest request;
    request.url = m_baseUrl;
    request.method = HttpMethod::POST;
    request.headers = m_headers;
    request.body = input.toJson().dump();
    request.timeoutMs = static_cast<int>(m_timeoutSeconds) * 1000;

    Logger::debugf("Sending request to ", m_baseUrl);
    Logger::debugf("Request body size: ", request.body.size(), " bytes");

    // 5. Send request with separated onData and onComplete handlers
    m_httpService->sendSseRequest(
        request,
        // onData: Incremental processing of SSE chunks
        [this](const HttpResponse& response) {
            this->handleStreamData(response);
        },
        // onComplete: Final processing when stream ends
        [this, onSuccess, onError](const HttpResponse& response) {
            this->handleStreamComplete(response, onSuccess, onError);
        },
        [this, onError](const AgentError& error) {
            Logger::errorf("SSE request error: ", error.fullMessage());
            cleanupPerRunSubscribers();
            if (onError) {
                onError(error.fullMessage());
            }
        });
}

void HttpAgent::cleanupPerRunSubscribers() {
    for (auto& subscriber : m_perRunSubscribers) {
        m_eventHandler->removeSubscriber(subscriber);
    }
    m_perRunSubscribers.clear();
}

void HttpAgent::handleStreamData(const HttpResponse& response) {
    // Feed data incrementally without clearing parser
    m_sseParser->feed(response.content);
    
    // Process all complete events available
    processAvailableEvents();
}

void HttpAgent::processAvailableEvents() {
    // Prepare middleware context
    MiddlewareContext middlewareContext(nullptr, nullptr);
    middlewareContext.currentMessages = &m_eventHandler->messages();
    middlewareContext.currentState = &m_eventHandler->state();

    // Process all available SSE events
    while (m_sseParser->hasEvent()) {
        try {
            const std::string &eventData = m_sseParser->nextEvent();
            if (eventData.empty()) {
                continue;
            }

            // Parse raw SSE data — parse_error here means a malformed/heartbeat
            // packet; skip it.  Any parse_error thrown later (e.g. inside
            // handleStateDelta) must reach the outer fatal handler, so isolate
            // this parse in its own inner try/catch.
            nlohmann::json eventJson;
            try {
                eventJson = nlohmann::json::parse(eventData);
            } catch (const nlohmann::json::parse_error& e) {
                Logger::warningf("[HttpAgent] Skipping malformed SSE event (JSON parse error): ", e.what());
                continue;
            }

            // Parse as Event object
            std::unique_ptr<Event> event(EventParser::parse(eventJson));

            if (!event) {
                continue;
            }
            // Process event through middleware
            if (!m_middlewareChain.empty()) {
                auto processedEvents = m_middlewareChain.processEvent(std::move(event), middlewareContext);

                bool shouldStop = false;
                for (auto& processedEvent : processedEvents) {
                    EventType processedType = processedEvent->type();
                    // Extract RunError message before move
                    std::string runErrorMsg;
                    if (processedType == EventType::RunError) {
                        runErrorMsg = static_cast<const RunErrorEvent*>(processedEvent.get())->message;
                    }
                    AgentStateMutation mutation = m_eventHandler->handleEvent(std::move(processedEvent));
                    if (mutation.hasChanges()) {
                        m_eventHandler->applyMutation(mutation);
                        // Update middleware context
                        middlewareContext.currentMessages = &m_eventHandler->messages();
                        middlewareContext.currentState = &m_eventHandler->state();
                    }
                    if (processedType == EventType::RunError) {
                        m_runErrorOccurred = true;
                        m_runErrorMessage = runErrorMsg.empty() ? "Agent reported a run error" : runErrorMsg;
                        shouldStop = true;
                        break;
                    }
                }
                if (shouldStop) {
                    break;
                }
            } else {
                // No middleware, process directly
                EventType eventType = event->type();
                // Extract RunError message before move
                std::string runErrorMsg;
                if (eventType == EventType::RunError) {
                    runErrorMsg = static_cast<const RunErrorEvent*>(event.get())->message;
                }
                AgentStateMutation mutation = m_eventHandler->handleEvent(std::move(event));
                if (mutation.hasChanges()) {
                    m_eventHandler->applyMutation(mutation);
                }
                if (eventType == EventType::RunError) {
                    m_runErrorOccurred = true;
                    m_runErrorMessage = runErrorMsg.empty() ? "Agent reported a run error" : runErrorMsg;
                    break;
                }
            }
        } catch (const std::exception& e) {
            Logger::errorf("[HttpAgent] Fatal error processing event: ", e.what());
            m_runErrorOccurred = true;
            m_runErrorMessage = std::string("Event processing error: ") + e.what();
            break;
        }
    }

    // Check for SSE parsing errors — treat as run error to prevent silent data loss.
    if (!m_sseParser->getLastError().empty() && !m_runErrorOccurred) {
        Logger::errorf("[HttpAgent] SSE parser error detected: ", m_sseParser->getLastError());
        m_runErrorOccurred = true;
        m_runErrorMessage = std::string("SSE parser error: ") + m_sseParser->getLastError();
    }
}

void HttpAgent::handleStreamComplete(const HttpResponse& response, AgentSuccessCallback onSuccess,
                                     AgentErrorCallback onError) {
    // Check if HTTP request succeeded
    if (!response.isSuccess()) {
        Logger::errorf("HTTP request failed with status: ", response.statusCode);
        cleanupPerRunSubscribers();
        if (onError) {
            try {
                onError("HTTP request failed with status: " + std::to_string(response.statusCode));
            } catch (const std::exception& ex) {
                Logger::errorf("[HttpAgent] onError callback threw: ", ex.what());
            } catch (...) {
                Logger::errorf("[HttpAgent] onError callback threw unknown exception");
            }
        }
        return;
    }

    Logger::info("Stream complete, flushing remaining data");

    // Flush any remaining data in parser buffer
    m_sseParser->flush();

    // If no error yet, process remaining events produced by flush
    if (!m_runErrorOccurred) {
        processAvailableEvents();
    }

    // If a RUN_ERROR event or fatal error occurred at any point during this run,
    // call onError instead of onSuccess — protocol requires these to be mutually exclusive.
    if (m_runErrorOccurred) {
        Logger::errorf("[HttpAgent] Run terminated with error: ", m_runErrorMessage);
        cleanupPerRunSubscribers();
        if (onError) {
            try {
                onError(m_runErrorMessage);
            } catch (const std::exception& ex) {
                Logger::errorf("[HttpAgent] onError callback threw: ", ex.what());
            } catch (...) {
                Logger::errorf("[HttpAgent] onError callback threw unknown exception");
            }
        }
        return;
    }

    // Collect results
    RunAgentResult result;
    result.newState = m_eventHandler->state();
    result.result = m_eventHandler->result();

    // Compute new messages as the delta: only messages added during this run
    for (const auto& msg : m_eventHandler->messages()) {
        if (m_preRunMessageIds.find(msg.id()) == m_preRunMessageIds.end()) {
            result.newMessages.push_back(msg);
        }
    }

    // Wrap the remaining path in try/catch so cleanupPerRunSubscribers always runs,
    // even if processResponse or the onSuccess callback throws.
    try {
        if (!m_middlewareChain.empty()) {
            Logger::infof("Processing response through ", m_middlewareChain.size(), " middlewares");
            MiddlewareContext middlewareContext(nullptr, nullptr);
            middlewareContext.currentMessages = &m_eventHandler->messages();
            middlewareContext.currentState = &m_eventHandler->state();
            result = m_middlewareChain.processResponse(result, middlewareContext);
        }

        if (onSuccess) {
            onSuccess(result);
        }
    } catch (const std::exception& e) {
        Logger::errorf("[HttpAgent] Error in success completion path: ", e.what());
        cleanupPerRunSubscribers();
        if (onError) {
            onError(std::string("Error completing run: ") + e.what());
        }
        return;
    }

    // Cleanup per-run subscribers after run completes
    cleanupPerRunSubscribers();
}

}  // namespace agui
