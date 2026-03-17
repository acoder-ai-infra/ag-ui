/**
 * @file test_http_agent.cpp
 * @brief HttpAgent end-to-end tests
 * 
 * Tests HttpAgent building, running, state management and subscriber management
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "agent/http_agent.h"
#include "core/error.h"
#include "core/event.h"
#include "core/subscriber.h"
#include "core/session_types.h"

using namespace agui;

class TestSubscriber : public IAgentSubscriber {
public:
    int textMessageStartCount = 0;
    AgentStateMutation onTextMessageStart(const TextMessageStartEvent& event,
                                          const AgentSubscriberParams& params) override {
        textMessageStartCount++;
        return AgentStateMutation();
    }
};

// HttpAgent Builder Tests
TEST(HttpAgentTest, BuilderBasicConstruction) {
    auto agent = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withAgentId(AgentId("test_agent_123"))
        .build();

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->agentId(), "test_agent_123");
}


TEST(HttpAgentTest, BuilderParameterConfiguration) {
    std::vector<Message> initialMessages = {
        Message("msg_1", MessageRole::User, "Hello"),
        Message("msg_2", MessageRole::Assistant, "Hi there!")
    };

    nlohmann::json initialState = {
        {"counter", 0},
        {"status", "ready"}
    };

    auto agent = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withAgentId(AgentId("agent_456"))
        .withBearerToken("test_token")
        .withTimeout(10)
        .withInitialMessages(initialMessages)
        .withInitialState(initialState)
        .build();

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->messages().size(), 2);
    EXPECT_EQ(agent->messages()[0].id(), "msg_1");
    EXPECT_EQ(agent->messages()[1].id(), "msg_2");
}


TEST(HttpAgentTest, BuilderMethodChaining) {
    auto agent = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withHeader("X-Custom-Header", "custom_value")
        .withHeader("X-Request-ID", "req_789")
        .withBearerToken("token_abc")
        .withTimeout(15)
        .withAgentId(AgentId("agent_chain"))
        .build();

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->agentId(), "agent_chain");
}

// Message Management Tests
TEST(HttpAgentTest, MessageManagement) {
    auto agent = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withAgentId(AgentId("agent_msg"))
        .build();

    EXPECT_TRUE(agent->messages().empty());

    Message msg1("msg_1", MessageRole::User, "Hello");
    agent->addMessage(msg1);
    EXPECT_EQ(agent->messages().size(), 1);
    EXPECT_EQ(agent->messages()[0].id(), "msg_1");

    Message msg2("msg_2", MessageRole::Assistant, "Hi");
    agent->addMessage(msg2);
    EXPECT_EQ(agent->messages().size(), 2);

    std::vector<Message> newMessages = {
        Message("msg_3", MessageRole::User, "New message 1"),
        Message("msg_4", MessageRole::Assistant, "New message 2"),
        Message("msg_5", MessageRole::User, "New message 3")
    };
    agent->setMessages(newMessages);
    EXPECT_EQ(agent->messages().size(), 3);
    EXPECT_EQ(agent->messages()[0].id(), "msg_3");
    EXPECT_EQ(agent->messages()[2].id(), "msg_5");
}


// Subscriber Management Tests
TEST(HttpAgentTest, SubscriberManagement) {
    auto agent = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withAgentId(AgentId("agent_sub"))
        .build();

    auto subscriber1 = std::make_shared<TestSubscriber>();
    auto subscriber2 = std::make_shared<TestSubscriber>();

    agent->subscribe(subscriber1);
    agent->subscribe(subscriber2);

    agent->unsubscribe(subscriber1);

    agent->clearSubscribers();
    
    // Test passes if no exceptions thrown
    SUCCEED();
}


TEST(HttpAgentTest, SubscriberNoneCallbackTriggering) {
    auto agent = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withAgentId(AgentId("agent_callback"))
        .build();

    auto subscriber = std::make_shared<TestSubscriber>();
    agent->subscribe(subscriber);

    // Note: This only tests subscriber registration, actual callback triggering requires real events
    EXPECT_EQ(subscriber->textMessageStartCount, 0);
}

// Multiple Agents Tests
TEST(HttpAgentTest, MultipleAgentInstances) {
    auto agent1 = HttpAgent::builder()
        .withUrl("http://localhost:8080")
        .withAgentId(AgentId("agent_1"))
        .build();

    auto agent2 = HttpAgent::builder()
        .withUrl("http://localhost:8081")
        .withAgentId(AgentId("agent_2"))
        .build();

    auto agent3 = HttpAgent::builder()
        .withUrl("http://localhost:8082")
        .withAgentId(AgentId("agent_3"))
        .build();

    EXPECT_EQ(agent1->agentId(), "agent_1");
    EXPECT_EQ(agent2->agentId(), "agent_2");
    EXPECT_EQ(agent3->agentId(), "agent_3");
}

// ─── Mock HTTP Service ───────────────────────────────────────────────────────

/**
 * @brief Synchronous mock that feeds pre-configured SSE chunks to HttpAgent
 *        without any real network I/O.
 */
class MockHttpService : public IHttpService {
public:
    // Chunks fed to onData callback, one by one
    std::vector<std::string> sseChunks;
    // When true, calls errorCallbackFunc instead of streaming
    bool simulateNetworkError = false;
    std::string networkErrorMessage = "Connection refused";

    void sendRequest(const HttpRequest&, HttpResponseCallback, HttpErrorCallback) override {}

    void sendSseRequest(const HttpRequest&, SseDataCallback onData,
                        SseCompleteCallback onComplete, HttpErrorCallback onError) override {
        if (simulateNetworkError) {
            if (onError) {
                onError(AgentError(ErrorType::Network, ErrorCode::NetworkError, networkErrorMessage));
            }
            return;
        }

        // Feed each SSE chunk synchronously
        for (const auto& chunk : sseChunks) {
            if (onData) {
                HttpResponse resp;
                resp.statusCode = 200;
                resp.content = chunk;
                onData(resp);
            }
        }

        // Signal stream completion
        if (onComplete) {
            HttpResponse resp;
            resp.statusCode = 200;
            resp.content = "success";
            onComplete(resp);
        }
    }
};

// Helper: build a minimal agent with the given mock service injected
static std::unique_ptr<HttpAgent> makeAgentWithMock(std::unique_ptr<MockHttpService> mock) {
    auto agent = HttpAgent::builder()
        .withUrl("http://mock-host/run")
        .withAgentId(AgentId("mock_agent"))
        .build();
    agent->setHttpService(std::move(mock));
    return agent;
}

// ─── Core Path Tests ─────────────────────────────────────────────────────────

TEST(HttpAgentTest, RunAgentCallsOnSuccessOnNormalCompletion) {
    auto mock = std::make_unique<MockHttpService>();
    mock->sseChunks = {
        "data: {\"type\":\"RUN_STARTED\",\"threadId\":\"t1\",\"runId\":\"r1\"}\n\n",
        "data: {\"type\":\"TEXT_MESSAGE_START\",\"messageId\":\"msg1\",\"role\":\"assistant\"}\n\n",
        "data: {\"type\":\"TEXT_MESSAGE_CONTENT\",\"messageId\":\"msg1\",\"delta\":\"Hello\"}\n\n",
        "data: {\"type\":\"TEXT_MESSAGE_END\",\"messageId\":\"msg1\"}\n\n",
        "data: {\"type\":\"RUN_FINISHED\",\"threadId\":\"t1\",\"runId\":\"r1\"}\n\n",
    };

    auto agent = makeAgentWithMock(std::move(mock));

    bool successCalled = false;
    bool errorCalled = false;
    RunAgentResult capturedResult;

    RunAgentParams params;
    params.threadId = "t1";
    params.runId = "r1";

    agent->runAgent(
        params,
        [&](const RunAgentResult& result) {
            successCalled = true;
            capturedResult = result;
        },
        [&](const std::string&) {
            errorCalled = true;
        });

    EXPECT_TRUE(successCalled);
    EXPECT_FALSE(errorCalled);
    // One new message should have been produced during this run
    ASSERT_EQ(capturedResult.newMessages.size(), 1);
    EXPECT_EQ(capturedResult.newMessages[0].content(), "Hello");
}

TEST(HttpAgentTest, RunAgentCallsOnErrorOnRunErrorEvent) {
    auto mock = std::make_unique<MockHttpService>();
    mock->sseChunks = {
        "data: {\"type\":\"RUN_STARTED\",\"threadId\":\"t1\",\"runId\":\"r1\"}\n\n",
        "data: {\"type\":\"RUN_ERROR\",\"message\":\"Something went wrong\"}\n\n",
    };

    auto agent = makeAgentWithMock(std::move(mock));

    bool successCalled = false;
    bool errorCalled = false;
    std::string capturedError;

    RunAgentParams params;
    params.threadId = "t1";
    params.runId = "r1";

    agent->runAgent(
        params,
        [&](const RunAgentResult&) {
            successCalled = true;
        },
        [&](const std::string& err) {
            errorCalled = true;
            capturedError = err;
        });

    EXPECT_FALSE(successCalled);
    EXPECT_TRUE(errorCalled);
    EXPECT_FALSE(capturedError.empty());
}

TEST(HttpAgentTest, RunAgentCallsOnErrorOnNetworkFailure) {
    auto mock = std::make_unique<MockHttpService>();
    mock->simulateNetworkError = true;
    mock->networkErrorMessage = "Connection refused";

    auto agent = makeAgentWithMock(std::move(mock));

    bool successCalled = false;
    bool errorCalled = false;

    RunAgentParams params;
    params.threadId = "t1";
    params.runId = "r1";

    agent->runAgent(
        params,
        [&](const RunAgentResult&) {
            successCalled = true;
        },
        [&](const std::string& error) {
            errorCalled = true;
            EXPECT_TRUE(error.find("Connection refused") != std::string::npos);
        });

    EXPECT_FALSE(successCalled);
    EXPECT_TRUE(errorCalled);
}

TEST(HttpAgentTest, RunAgentSkipsMalformedJsonAndSucceeds) {
    auto mock = std::make_unique<MockHttpService>();
    mock->sseChunks = {
        // Malformed JSON in a data line — should be skipped (L-1 fix)
        "data: {not valid json}\n\n",
        "data: {\"type\":\"RUN_STARTED\",\"threadId\":\"t1\",\"runId\":\"r1\"}\n\n",
        "data: {\"type\":\"TEXT_MESSAGE_START\",\"messageId\":\"msg1\",\"role\":\"assistant\"}\n\n",
        "data: {\"type\":\"TEXT_MESSAGE_CONTENT\",\"messageId\":\"msg1\",\"delta\":\"World\"}\n\n",
        "data: {\"type\":\"TEXT_MESSAGE_END\",\"messageId\":\"msg1\"}\n\n",
        "data: {\"type\":\"RUN_FINISHED\",\"threadId\":\"t1\",\"runId\":\"r1\"}\n\n",
    };

    auto agent = makeAgentWithMock(std::move(mock));

    bool successCalled = false;
    bool errorCalled = false;

    RunAgentParams params;
    params.threadId = "t1";
    params.runId = "r1";

    agent->runAgent(
        params,
        [&](const RunAgentResult& result) {
            successCalled = true;
            EXPECT_EQ(result.newMessages.size(), 1);
        },
        [&](const std::string&) {
            errorCalled = true;
        });

    EXPECT_TRUE(successCalled);
    EXPECT_FALSE(errorCalled);
}
