#include "core/subscriber.h"
#include "logger.h"
#include <algorithm>

namespace agui {

// EventHandler implementation

EventHandler::EventHandler(std::vector<Message> messages, const std::string &state,
                           std::vector<std::shared_ptr<IAgentSubscriber>> subscribers)
    : m_messages(std::move(messages)),
      m_subscribers(std::move(subscribers)),
      m_state(state.empty() ? "{}" : state) {}

AgentStateMutation EventHandler::handleEvent(std::unique_ptr<Event> event) {
    if (!event) {
        return AgentStateMutation();
    }

    EventType type = event->type();

    // Step 1: Invoke generic onEvent callback first
    AgentStateMutation genericMutation = notifySubscribers(
        [&](IAgentSubscriber* sub, const AgentSubscriberParams& params) { return sub->onEvent(*event, params); });

    // Step 2: Check stopPropagation flag
    if (genericMutation.stopPropagation) {
        return genericMutation;
    }

    // Step 3: Execute default event handling
    switch (type) {
        case EventType::TextMessageStart:
            handleTextMessageStart(*static_cast<TextMessageStartEvent*>(event.get()));
            break;
        case EventType::TextMessageContent:
            handleTextMessageContent(*static_cast<TextMessageContentEvent*>(event.get()));
            break;
        case EventType::TextMessageEnd:
            handleTextMessageEnd(*static_cast<TextMessageEndEvent*>(event.get()));
            break;
        case EventType::TextMessageChunk:
            handleTextMessageChunk(*static_cast<TextMessageChunkEvent*>(event.get()));
            break;
        case EventType::ThinkingTextMessageStart:
            handleThinkingTextMessageStart(*static_cast<ThinkingTextMessageStartEvent*>(event.get()));
            break;
        case EventType::ThinkingTextMessageContent:
            handleThinkingTextMessageContent(*static_cast<ThinkingTextMessageContentEvent*>(event.get()));
            break;
        case EventType::ThinkingTextMessageEnd:
            handleThinkingTextMessageEnd(*static_cast<ThinkingTextMessageEndEvent*>(event.get()));
            break;
        case EventType::ToolCallStart:
            handleToolCallStart(*static_cast<ToolCallStartEvent*>(event.get()));
            break;
        case EventType::ToolCallArgs:
            handleToolCallArgs(*static_cast<ToolCallArgsEvent*>(event.get()));
            break;
        case EventType::ToolCallEnd:
            handleToolCallEnd(*static_cast<ToolCallEndEvent*>(event.get()));
            break;
        case EventType::ToolCallChunk:
            handleToolCallChunk(*static_cast<ToolCallChunkEvent*>(event.get()));
            break;
        case EventType::ToolCallResult:
            handleToolCallResult(*static_cast<ToolCallResultEvent*>(event.get()));
            break;
        case EventType::StateSnapshot:
            handleStateSnapshot(*static_cast<StateSnapshotEvent*>(event.get()));
            break;
        case EventType::StateDelta:
            handleStateDelta(*static_cast<StateDeltaEvent*>(event.get()));
            break;
        case EventType::MessagesSnapshot:
            handleMessagesSnapshot(*static_cast<MessagesSnapshotEvent*>(event.get()));
            break;
        case EventType::RunStarted:
            handleRunStarted(*static_cast<RunStartedEvent*>(event.get()));
            break;
        case EventType::RunFinished:
            handleRunFinished(*static_cast<RunFinishedEvent*>(event.get()));
            break;
        case EventType::RunError:
            handleRunError(*static_cast<RunErrorEvent*>(event.get()));
            break;
        case EventType::ActivitySnapshot:
            handleActivitySnapshot(*static_cast<ActivitySnapshotEvent*>(event.get()));
            break;
        case EventType::ActivityDelta:
            handleActivityDelta(*static_cast<ActivityDeltaEvent*>(event.get()));
            break;

        default:
            break;
    }

    // Step 4: Invoke type-specific subscriber callbacks
    AgentStateMutation specificMutation;

    switch (type) {
        case EventType::TextMessageStart:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onTextMessageStart(*static_cast<TextMessageStartEvent*>(event.get()), params);
            });
            break;

        case EventType::TextMessageContent: {
            auto* e = static_cast<TextMessageContentEvent*>(event.get());
            const std::string& buffer = m_textBuffers[e->messageId];
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onTextMessageContent(*e, buffer, params);
            });
            break;
        }

        case EventType::TextMessageEnd:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onTextMessageEnd(*static_cast<TextMessageEndEvent*>(event.get()), params);
            });
            break;

        case EventType::TextMessageChunk:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onTextMessageChunk(*static_cast<TextMessageChunkEvent*>(event.get()), params);
            });
            break;

        case EventType::ThinkingTextMessageStart:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onThinkingTextMessageStart(*static_cast<ThinkingTextMessageStartEvent*>(event.get()),
                                                       params);
            });
            break;

        case EventType::ThinkingTextMessageContent: {
            auto* e = static_cast<ThinkingTextMessageContentEvent*>(event.get());
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onThinkingTextMessageContent(*e, m_thinkingBuffer, params);
            });
            break;
        }

        case EventType::ThinkingTextMessageEnd:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onThinkingTextMessageEnd(*static_cast<ThinkingTextMessageEndEvent*>(event.get()), params);
            });
            break;

        case EventType::ToolCallStart:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onToolCallStart(*static_cast<ToolCallStartEvent*>(event.get()), params);
            });
            break;

        case EventType::ToolCallArgs: {
            auto* e = static_cast<ToolCallArgsEvent*>(event.get());
            const std::string& buffer = m_toolCallArgsBuffers[e->toolCallId];
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onToolCallArgs(*e, buffer, params);
            });
            break;
        }

        case EventType::ToolCallEnd:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onToolCallEnd(*static_cast<ToolCallEndEvent*>(event.get()), params);
            });
            break;

        case EventType::ToolCallChunk:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onToolCallChunk(*static_cast<ToolCallChunkEvent*>(event.get()), params);
            });
            break;

        case EventType::ToolCallResult:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onToolCallResult(*static_cast<ToolCallResultEvent*>(event.get()), params);
            });
            break;

        case EventType::ThinkingStart:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onThinkingStart(*static_cast<ThinkingStartEvent*>(event.get()), params);
            });
            break;

        case EventType::ThinkingEnd:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onThinkingEnd(*static_cast<ThinkingEndEvent*>(event.get()), params);
            });
            break;

        case EventType::StateSnapshot:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onStateSnapshot(*static_cast<StateSnapshotEvent*>(event.get()), params);
            });
            break;

        case EventType::StateDelta:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onStateDelta(*static_cast<StateDeltaEvent*>(event.get()), params);
            });
            break;

        case EventType::MessagesSnapshot:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onMessagesSnapshot(*static_cast<MessagesSnapshotEvent*>(event.get()), params);
            });
            break;

        case EventType::RunStarted:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onRunStarted(*static_cast<RunStartedEvent*>(event.get()), params);
            });
            break;

        case EventType::RunFinished:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onRunFinished(*static_cast<RunFinishedEvent*>(event.get()), params);
            });
            break;

        case EventType::RunError:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onRunError(*static_cast<RunErrorEvent*>(event.get()), params);
            });
            break;

        case EventType::ActivitySnapshot:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onActivitySnapshot(*static_cast<ActivitySnapshotEvent*>(event.get()), params);
            });
            break;

        case EventType::ActivityDelta:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onActivityDelta(*static_cast<ActivityDeltaEvent*>(event.get()), params);
            });
            break;

        case EventType::StepStarted:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onStepStarted(*static_cast<StepStartedEvent*>(event.get()), params);
            });
            break;

        case EventType::StepFinished:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onStepFinished(*static_cast<StepFinishedEvent*>(event.get()), params);
            });
            break;

        case EventType::Raw:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onRawEvent(*static_cast<RawEvent*>(event.get()), params);
            });
            break;

        case EventType::Custom:
            specificMutation = notifySubscribers([&](IAgentSubscriber* sub, const AgentSubscriberParams& params) {
                return sub->onCustomEvent(*static_cast<CustomEvent*>(event.get()), params);
            });
            break;
        default:
            break;
    }

    return specificMutation;
}

void EventHandler::applyMutation(const AgentStateMutation& mutation) {
    if (mutation.messages.has_value()) {
        m_messages = mutation.messages.value();
        notifyMessagesChanged();
    }

    if (mutation.state.has_value()) {
        m_state = mutation.state.value().dump();
        notifyStateChanged();
    }
}

void EventHandler::addSubscriber(std::shared_ptr<IAgentSubscriber> subscriber) {
    if (subscriber) {
        m_subscribers.push_back(subscriber);
    }
}

void EventHandler::removeSubscriber(std::shared_ptr<IAgentSubscriber> subscriber) {
    m_subscribers.erase(std::remove(m_subscribers.begin(), m_subscribers.end(), subscriber), m_subscribers.end());
}

void EventHandler::clearSubscribers() {
    m_subscribers.clear();
}

void EventHandler::handleTextMessageStart(const TextMessageStartEvent& event) {
    Message* existingMessage = findMessage(event.messageId);
    if (!existingMessage) {
        // Reuse any placeholder message created by TOOL_CALL_START when IDs match.
        Message message = Message::createWithId(event.messageId, Message::roleFromString(event.role), "");
        m_messages.push_back(message);
        notifyNewMessage(m_messages.back());
        notifyMessagesChanged();
    } else {
        existingMessage->setRole(Message::roleFromString(event.role));
    }
    m_textBuffers[event.messageId] = "";
}

void EventHandler::handleTextMessageContent(const TextMessageContentEvent& event) {
    m_textBuffers[event.messageId] += event.delta;
    Message* msg = findMessage(event.messageId);
    if (msg) {
        msg->appendContent(event.delta);
    }
}

void EventHandler::handleTextMessageEnd(const TextMessageEndEvent& event) {
    m_textBuffers.erase(event.messageId);
    notifyMessagesChanged();
}

void EventHandler::handleTextMessageChunk(const TextMessageChunkEvent& event) {
    const MessageId targetMessageId = event.messageId.empty() ? m_lastTextChunkMessageId : event.messageId;
    if (targetMessageId.empty()) {
        return;
    }

    m_lastTextChunkMessageId = targetMessageId;

    Message* message = findMessage(targetMessageId);
    if (!message) {
        Message newMessage = Message::createWithId(
            targetMessageId,
            Message::roleFromString(event.role.value_or("assistant")),
            "");
        if (event.name.has_value()) {
            newMessage.setName(event.name.value());
        }
        m_messages.push_back(newMessage);
        notifyNewMessage(m_messages.back());
        message = &m_messages.back();
    } else if (event.role.has_value()) {
        message->setRole(Message::roleFromString(event.role.value()));
    }

    if (event.name.has_value()) {
        message->setName(event.name.value());
    }

    m_textBuffers[targetMessageId] += event.delta;
    message->appendContent(event.delta);
}

void EventHandler::handleThinkingTextMessageStart(const ThinkingTextMessageStartEvent&) {
    // Thinking messages are not persisted to message history; clear the dedicated buffer
    m_thinkingBuffer.clear();
}

void EventHandler::handleThinkingTextMessageContent(const ThinkingTextMessageContentEvent& event) {
    m_thinkingBuffer += event.delta;
}

void EventHandler::handleThinkingTextMessageEnd(const ThinkingTextMessageEndEvent&) {
    m_thinkingBuffer.clear();
}

void EventHandler::handleToolCallStart(const ToolCallStartEvent& event) {
    Message* msg = nullptr;

    // Full scan: parentMessageId may refer to any message in history, not just the last one.
    if (event.parentMessageId.has_value()) {
        msg = findMessage(event.parentMessageId.value());
    }

    if (!msg) {
        const MessageId targetMessageId =
            event.parentMessageId.has_value() ? event.parentMessageId.value() : event.toolCallId;
        Message message = Message::createWithId(targetMessageId, MessageRole::Assistant, "");
        m_messages.push_back(message);
        msg = &m_messages.back();
    }

    ToolCall toolCall;
    toolCall.id = event.toolCallId;
    toolCall.function.name = event.toolCallName;
    toolCall.function.arguments = "";

    msg->addToolCall(toolCall);
    m_toolCallArgsBuffers[event.toolCallId] = "";
    notifyNewToolCall(toolCall);
}

void EventHandler::handleToolCallArgs(const ToolCallArgsEvent& event) {
    m_toolCallArgsBuffers[event.toolCallId] += event.delta;
    appendEventDelta(event.toolCallId, event.delta);
}

void EventHandler::handleToolCallEnd(const ToolCallEndEvent& event) {
    m_toolCallArgsBuffers.erase(event.toolCallId);
    notifyMessagesChanged();
}

void EventHandler::handleToolCallChunk(const ToolCallChunkEvent& event) {
    const ToolCallId targetToolCallId = event.toolCallId.empty() ? m_lastToolCallChunkId : event.toolCallId;
    if (targetToolCallId.empty()) {
        return;
    }

    m_lastToolCallChunkId = targetToolCallId;

    Message* targetMessage = findMessageContainingToolCall(targetToolCallId);
    if (!targetMessage) {
        if (!event.toolCallName.has_value()) {
            return;
        }

        if (event.parentMessageId.has_value()) {
            targetMessage = findMessage(event.parentMessageId.value());
        }

        if (!targetMessage) {
            const MessageId targetMessageId =
                event.parentMessageId.has_value() ? event.parentMessageId.value() : targetToolCallId;
            Message message = Message::createWithId(targetMessageId, MessageRole::Assistant, "");
            m_messages.push_back(message);
            targetMessage = &m_messages.back();
            notifyNewMessage(*targetMessage);
        }

        ToolCall toolCall;
        toolCall.id = targetToolCallId;
        toolCall.function.name = event.toolCallName.value();
        toolCall.function.arguments = "";
        targetMessage->addToolCall(toolCall);
        notifyNewToolCall(toolCall);
    }

    m_toolCallArgsBuffers[targetToolCallId] += event.delta;
    appendEventDelta(targetToolCallId, event.delta);
}

void EventHandler::handleStateSnapshot(const StateSnapshotEvent& event) {
    m_state = event.snapshot.dump();
    notifyStateChanged();
}

void EventHandler::handleStateDelta(const StateDeltaEvent& event) {
    // Re-throw on failure: a state divergence is a fatal condition. The caller
    // (processAvailableEvents) will catch it and terminate the run with an error.
    StateManager stateManager(nlohmann::json::parse(m_state));
    stateManager.applyPatch(event.delta);
    m_state = stateManager.currentState().dump();
    notifyStateChanged();
}

void EventHandler::handleMessagesSnapshot(const MessagesSnapshotEvent& event) {
    m_messages = event.messages;
    notifyMessagesChanged();
}

void EventHandler::handleRunStarted(const RunStartedEvent&) {
}

void EventHandler::handleRunFinished(const RunFinishedEvent& event) {
    if (!event.result.is_null()) {
        m_result = event.result.dump();
    }
}

void EventHandler::handleRunError(const RunErrorEvent& event) {
    // The RunError event has been notified to the business layer, no additional processing here.
    // Upon receiving the RunError event, the business layer should decide how to handle it based
    // on the business scenario, such as terminating the request, displaying page notifications, etc.
}

AgentStateMutation EventHandler::notifySubscribers(
    std::function<AgentStateMutation(IAgentSubscriber*, const AgentSubscriberParams&)> notifyFunc) {
    AgentStateMutation finalMutation;
    AgentSubscriberParams params = createParams();

    for (auto& subscriber : m_subscribers) {
        try {
            AgentStateMutation mutation = notifyFunc(subscriber.get(), params);
            
            if (mutation.messages.has_value()) {
                finalMutation.messages = mutation.messages;
            }
            if (mutation.state.has_value()) {
                finalMutation.state = mutation.state;
            }
            
            if (mutation.stopPropagation) {
                finalMutation.stopPropagation = true;
                break;
            }
        } catch (const std::exception& e) {
            Logger::errorf("notifySubscribers: subscriber error: ", e.what());
        }
    }

    return finalMutation;
}

void EventHandler::notifyNewMessage(const Message& message) {
    AgentSubscriberParams params = createParams();
    for (auto& subscriber : m_subscribers) {
        try {
            subscriber->onNewMessage(message, params);
        } catch (const std::exception& e) {
            Logger::errorf("notifyNewMessage: subscriber error: ", e.what());
        }
    }
}

void EventHandler::notifyNewToolCall(const ToolCall& toolCall) {
    AgentSubscriberParams params = createParams();
    for (auto& subscriber : m_subscribers) {
        try {
            subscriber->onNewToolCall(toolCall, params);
        } catch (const std::exception& e) {
            Logger::errorf("notifyNewToolCall: subscriber error: ", e.what());
        }
    }
}

void EventHandler::notifyMessagesChanged() {
    AgentSubscriberParams params = createParams();
    for (auto& subscriber : m_subscribers) {
        try {
            subscriber->onMessagesChanged(params);
        } catch (const std::exception& e) {
            Logger::errorf("notifyMessagesChanged: subscriber error: ", e.what());
        }
    }
}

void EventHandler::notifyStateChanged() {
    AgentSubscriberParams params = createParams();
    for (auto& subscriber : m_subscribers) {
        try {
            subscriber->onStateChanged(params);
        } catch (const std::exception& e) {
            Logger::errorf("notifyStateChanged: subscriber error: ", e.what());
        }
    }
}

Message* EventHandler::findMessage(const MessageId& id) {
    for (auto& msg : m_messages) {
        if (msg.id() == id) {
            return &msg;
        }
    }
    return nullptr;
}

Message* EventHandler::findMessageContainingToolCall(const ToolCallId& toolCallId) {
    for (auto& msg : m_messages) {
        for (const auto& toolCall : msg.toolCalls()) {
            if (toolCall.id == toolCallId) {
                return &msg;
            }
        }
    }
    return nullptr;
}

void EventHandler::appendEventDelta(const ToolCallId& toolCallId, const std::string &delta) {
    Message* msg = findMessageContainingToolCall(toolCallId);
    if (!msg) {
        return;
    }
    msg->appendEventDelta(toolCallId, delta);
}

AgentSubscriberParams EventHandler::createParams() const {
    return AgentSubscriberParams(&m_messages, &m_state);
}

void EventHandler::handleToolCallResult(const ToolCallResultEvent& event) {
    Message toolMessage = event.messageId.empty()
        ? Message::create(MessageRole::Tool, event.content, "", event.toolCallId)
        : Message::createWithId(event.messageId, MessageRole::Tool, event.content, "", event.toolCallId);
    m_messages.push_back(toolMessage);
    notifyNewMessage(toolMessage);
    notifyMessagesChanged();
}

void EventHandler::handleActivitySnapshot(const ActivitySnapshotEvent& event) {
    Message* existing = findMessage(event.messageId);

    if (!existing) {
        // Create a new Activity message with the snapshot content
        Message activityMsg = Message::createWithId(event.messageId, MessageRole::Activity,
                                                    event.content.dump());
        activityMsg.setActivityType(event.activityType);
        m_messages.push_back(activityMsg);
        notifyNewMessage(m_messages.back());
    } else if (event.replace) {
        // Replace existing activity message content
        existing->setContent(event.content.dump());
        existing->setActivityType(event.activityType);
    }

    notifyMessagesChanged();
}

void EventHandler::handleActivityDelta(const ActivityDeltaEvent& event) {
    Message* existing = findMessage(event.messageId);
    if (!existing) {
        return;  // silently skip, consistent with TypeScript
    }

    if (existing->role() != MessageRole::Activity) {
        Logger::warningf("handleActivityDelta: message '", event.messageId, "' is not an activity message");
        return;
    }

    try {
        // Default to empty object if content is absent, consistent with TypeScript (content ?? {})
        nlohmann::json currentContent = existing->content().empty()
            ? nlohmann::json::object()
            : nlohmann::json::parse(existing->content());

        // Convert vector<JsonPatchOp> to JSON array for StateManager
        nlohmann::json patchJson = nlohmann::json::array();
        for (const auto& op : event.patch) {
            patchJson.push_back(op.toJson());
        }

        StateManager stateManager(currentContent);
        stateManager.applyPatch(patchJson);
        existing->setContent(stateManager.currentState().dump());
        existing->setActivityType(event.activityType);  // sync activityType from delta event
    } catch (const std::exception& e) {
        Logger::errorf("handleActivityDelta: failed to apply patch for '", event.messageId, "': ", e.what());
        return;
    }

    notifyMessagesChanged();
}

}  // namespace agui
