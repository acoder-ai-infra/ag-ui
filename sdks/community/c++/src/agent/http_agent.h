#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "agent.h"
#include "core/event.h"
#include "core/session_types.h"
#include "core/subscriber.h"
#include "http/http_service.h"
#include "stream/sse_parser.h"
#include "middleware/middleware.h"

#include <nlohmann/json.hpp>

namespace agui {

/**
 * @brief HTTP Agent implementation for communicating with Agent server via HTTP/SSE
 */
class HttpAgent : public IAgent {
public:
    /**
     * @brief Builder class for constructing HttpAgent using Builder pattern
     */
    class Builder {
    public:
        Builder();

        /**
         * @brief Set base URL
         * @param url Base URL
         * @return Builder reference
         */
        Builder& withUrl(const std::string& url);

        /**
         * @brief Add HTTP header
         * @param name Header name
         * @param value Header value
         * @return Builder reference
         */
        Builder& withHeader(const std::string& name, const std::string& value);

        /**
         * @brief Set Bearer token
         * @param token Token value
         * @return Builder reference
         */
        Builder& withBearerToken(const std::string& token);

        /**
         * @brief Set timeout
         * @param seconds Timeout in seconds
         * @return Builder reference
         */
        Builder& withTimeout(uint32_t seconds);

        /**
         * @brief Set Agent ID
         * @param id Agent ID
         * @return Builder reference
         */
        Builder& withAgentId(const AgentId& id);

        /**
         * @brief Set initial messages
         * @param messages Initial message list
         * @return Builder reference
         */
        Builder& withInitialMessages(const std::vector<Message>& messages);

        /**
         * @brief Set initial state
         * @param state Initial state
         * @return Builder reference
         */
        Builder& withInitialState(const nlohmann::json& state);

        /**
         * @brief Build HttpAgent
         * @return HttpAgent smart pointer
         */
        std::unique_ptr<HttpAgent> build();

    private:
        std::string m_url;
        std::map<std::string, std::string> m_headers;
        uint32_t m_timeout;
        AgentId m_agentId;
        std::vector<Message> m_initialMessages;
        std::string m_initialState;
    };

    /**
     * @brief Create Builder
     * @return Builder object
     */
    static Builder builder();

    /**
     * @brief Destructor
     */
    virtual ~HttpAgent();

    // IAgent interface implementation
    
    /**
     * @brief Run the agent with the given parameters
     * 
     * @warning BLOCKING CALL - This method blocks the calling thread until completion
     * 
     * This is a synchronous blocking call using libcurl. The blocking behavior is intentional
     * to provide maximum flexibility for different threading models (worker threads, thread pools,
     * async frameworks like Boost.Asio/Qt/libuv, etc.). See README.md "Architecture & Design Decisions"
     * section for detailed rationale and usage patterns.
     * 
     * @param params Run parameters including input messages and state
     * @param onSuccess Callback invoked when agent completes successfully
     * @param onError Callback invoked when an error occurs
     */
    void runAgent(const RunAgentParams& params, AgentSuccessCallback onSuccess, AgentErrorCallback onError) override;

    AgentId agentId() const override;

    // State access (delegated to EventHandler)

    /**
     * @brief Get messages
     * @return Const reference to message list
     */
    const std::vector<Message>& messages() const;

    /**
     * @brief Get state
     * @return Const reference to state
     */
    const std::string& state() const;

    // State modification (delegated to EventHandler)

    /**
     * @brief Add message
     * @param message Message to add
     */
    void addMessage(const Message& message);

    /**
     * @brief Set messages
     * @param messages New message list
     */
    void setMessages(const std::vector<Message>& messages);

    /**
     * @brief Set state
     * @param state New state
     */
    void setState(const nlohmann::json& state);

    // Subscriber management (delegated to EventHandler)

    /**
     * @brief Add subscriber
     * @param subscriber Subscriber smart pointer
     */
    void subscribe(std::shared_ptr<IAgentSubscriber> subscriber);

    /**
     * @brief Remove subscriber
     * @param subscriber Subscriber smart pointer
     */
    void unsubscribe(std::shared_ptr<IAgentSubscriber> subscriber);

    /**
     * @brief Clear all subscribers
     */
    void clearSubscribers();

    // Middleware management

    /**
     * @brief Add middleware
     * @param middleware Middleware smart pointer
     * @return HttpAgent reference for chaining
     */
    HttpAgent& use(std::shared_ptr<IMiddleware> middleware);

    /**
     * @brief Get middleware chain
     * @return Reference to middleware chain
     */
    MiddlewareChain& middlewareChain();

    /**
     * @brief Replace the HTTP service (dependency injection, useful for testing)
     * @param service Custom IHttpService implementation
     */
    void setHttpService(std::unique_ptr<IHttpService> service);

private:
    /**
     * @brief Constructor (private)
     */
    HttpAgent(const std::string& baseUrl, const std::map<std::string, std::string>& headers, const AgentId& agentId,
              const std::vector<Message>& initialMessages, const std::string& initialState, uint32_t timeoutSeconds);

    /**
     * @brief Handle streaming data chunk (called for each SSE data chunk)
     */
    void handleStreamData(const HttpResponse& response);

    /**
     * @brief Handle stream completion (called when SSE stream ends)
     */
    void handleStreamComplete(const HttpResponse& response, AgentSuccessCallback onSuccess, AgentErrorCallback onError);

    /**
     * @brief Process all available events from SSE parser
     */
    void processAvailableEvents();

    /**
     * @brief Remove all per-run subscribers from EventHandler and clear the tracking list.
     *        Called from all runAgent() exit paths to prevent subscriber accumulation.
     */
    void cleanupPerRunSubscribers();

    std::string m_baseUrl;
    std::map<std::string, std::string> m_headers;
    AgentId m_agentId;
    uint32_t m_timeoutSeconds;

    // Persistent EventHandler
    std::shared_ptr<EventHandler> m_eventHandler;

    std::unique_ptr<IHttpService> m_httpService;
    std::unique_ptr<SseParser> m_sseParser;
    
    // Middleware chain
    MiddlewareChain m_middlewareChain;

    // Per-run subscribers added via RunAgentParams; removed after each runAgent() call
    std::vector<std::shared_ptr<IAgentSubscriber>> m_perRunSubscribers;

    // Message IDs present before the run starts; used to compute the newMessages delta
    std::set<MessageId> m_preRunMessageIds;

    // Set to true when a RUN_ERROR event or a fatal event-processing error is
    // encountered during streaming
    bool m_runErrorOccurred = false;
    std::string m_runErrorMessage;
};

}  // namespace agui
