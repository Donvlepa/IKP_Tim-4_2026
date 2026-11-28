#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstddef>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "TwrReceiver.h"
#include "MessageIO.h"
#include "TestSuites.h"

namespace
{
    constexpr const char* LOCAL_ADDRESS =
        "127.0.0.1";

    constexpr uint32_t TEST_TIMEOUT_MS = 5000;

    // Checks a test condition.
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr
                << "[FAIL] " << message << "\n";

            std::abort();
        }
    }

    // Checks an operation result.
    void checkResult(int result, const char* message)
    {
        if (result != 0)
        {
            std::cerr
                << "[FAIL] " << message
                << " | Error: "
                << result << "\n";

            std::abort();
        }
    }

    // Owns the two receiver threads used by the tests.
    struct ReceiverTestServer
    {
        ConnectionHandoff handoff;

        TurReceiver tur;
        TwrReceiver twr;

        ReceiverTestServer()
            : tur(handoff),
            twr(handoff)
        {
        }

        ~ReceiverTestServer()
        {
            tur.stop();

            handoff.seal();

            twr.stop();
        }
    };

    // Starts both receiver threads.
    void startServer(ReceiverTestServer& server)
    {
        checkResult(
            server.twr.start(),
            "TWR startup failed"
        );

        checkResult(
            server.tur.start(
                LOCAL_ADDRESS, 0),
            "TUR startup failed"
        );

        check(
            server.tur.getPort() != 0,
            "Receiver server port is invalid"
        );
    }

    // Stops the producer before stopping the consumer.
    void stopServer(ReceiverTestServer& server)
    {
        checkResult(
            server.tur.stop(),
            "TUR shutdown failed"
        );

        server.handoff.seal();

        checkResult(
            server.twr.stop(),
            "TWR shutdown failed"
        );

        check(
            server.handoff.isEmpty(),
            "Handoff queue retained connections"
        );

        const TurStatistics turStatistics =
            server.tur.getStatistics();

        const TwrStatistics twrStatistics =
            server.twr.getStatistics();

        check(
            turStatistics.activeUsers == 0 &&
            turStatistics.pendingConnections == 0,
            "TUR retained active connections"
        );

        check(
            twrStatistics.activeWorkers == 0,
            "TWR retained active workers"
        );
    }

    // Waits for a condition while both threads run independently.
    template<typename Predicate>
    void waitUntil(
        ReceiverTestServer& server,
        Predicate predicate
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            const TurStatistics turStatistics =
                server.tur.getStatistics();

            const TwrStatistics twrStatistics =
                server.twr.getStatistics();

            checkResult(
                turStatistics.lastError,
                "TUR reported an internal error"
            );

            checkResult(
                twrStatistics.lastError,
                "TWR reported an internal error"
            );

            if (predicate(
                turStatistics,
                twrStatistics))
            {
                return;
            }

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Receiver condition timed out"
            );

            Sleep(5);
        }
    }

    // Connects a test client to TUR.
    void connectClient(
        ReceiverTestServer& server,
        TcpClient& client
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                server.tur.getPort()),
            "Test client connection failed"
        );
    }

    // Sends a complete protocol message.
    template<typename Payload>
    void sendChecked(
        TcpClient& client,
        const Payload& payload
    )
    {
        std::size_t bytesSent = 0;

        checkResult(
            MessageIO::sendMessage(
                client.getSocket(),
                payload,
                bytesSent,
                TEST_TIMEOUT_MS),
            "Test message sending failed"
        );

        check(
            bytesSent > 0,
            "No message bytes were sent"
        );
    }

    // Sends a user registration.
    void registerUser(TcpClient& client)
    {
        RegisterUrPayload registration;

        sendChecked(client, registration);
    }

    // Sends a worker registration.
    void registerWorker(
        TcpClient& client,
        uint64_t workerId
    )
    {
        RegisterWrPayload registration;

        registration.workerId = workerId;

        sendChecked(client, registration);
    }

    // Sends a heartbeat message.
    void sendHeartbeat(TcpClient& client)
    {
        HeartbeatPayload heartbeat;

        sendChecked(client, heartbeat);
    }

    // Sends a diagnostic result message.
    void sendResult(
        TcpClient& client,
        uint64_t requestId,
        uint64_t attemptId
    )
    {
        const uint8_t data[] =
        {
            0, 10, 20, 255
        };

        ResultPayload result;

        result.lbRequestId = requestId;
        result.attemptId = attemptId;

        result.resultSize =
            static_cast<uint32_t>(
                sizeof(data)
                );

        result.result = data;

        sendChecked(client, result);
    }

    // Sends a user request.
    void sendRequest(
        TcpClient& client,
        uint64_t requestId
    )
    {
        const uint8_t data[] =
        {
            10, 20, 30
        };

        RequestPayload request;

        request.clientRequestId = requestId;
        request.priority = Priority::HIGH;

        request.dataSize =
            static_cast<uint32_t>(
                sizeof(data)
                );

        request.data = data;

        sendChecked(client, request);
    }

    // Retrieves a synchronized worker snapshot.
    TwrWorkerInfo getWorker(
        TwrReceiver& twr,
        uint64_t workerId
    )
    {
        TwrWorkerInfo information;

        check(
            twr.getWorker(
                workerId, information),
            "Expected worker was not found"
        );

        check(
            information.workerId == workerId,
            "Incorrect worker identifier"
        );

        check(
            information.sessionId != 0,
            "Worker session identifier is invalid"
        );

        return information;
    }

    // Verifies that the remote TCP connection was closed.
    void expectPeerClosed(TcpClient& client)
    {
        MessageReceiver receiver;

        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            int socketError = 0;

            const ReceiveStatus status =
                receiver.receive(
                    client.getSocket(),
                    socketError
                );

            if (status ==
                ReceiveStatus::ConnectionClosed)
            {
                return;
            }

            // A reset also indicates a terminated connection.
            if (status == ReceiveStatus::SocketError &&
                socketError == WSAECONNRESET)
            {
                return;
            }

            check(
                status == ReceiveStatus::WouldBlock,
                "Unexpected data or socket error during closure"
            );

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Peer connection closure timed out"
            );

            Sleep(5);
        }
    }

    // Tests initial state, startup and restart.
    void testLifecycle()
    {
        ConnectionHandoff handoff;

        TwrReceiver twr(handoff);

        const TwrStatistics initial =
            twr.getStatistics();

        check(
            initial.activeWorkers == 0 &&
            initial.registeredWorkers == 0 &&
            initial.rejectedWorkers == 0 &&
            initial.disconnectedWorkers == 0 &&
            initial.observedHeartbeats == 0 &&
            initial.observedResults == 0 &&
            initial.lastError == 0,
            "Incorrect initial TWR state"
        );

        TwrWorkerInfo information;

        check(
            !twr.getWorker(1, information),
            "Inactive TWR contains a worker"
        );

        checkResult(
            twr.stop(),
            "Inactive TWR cleanup failed"
        );

        checkResult(
            twr.start(),
            "TWR startup failed"
        );

        check(
            twr.start() == WSAEINVAL,
            "Duplicate TWR startup was accepted"
        );

        checkResult(
            twr.stop(),
            "TWR shutdown failed"
        );

        checkResult(
            twr.start(),
            "TWR restart failed"
        );

        checkResult(
            twr.stop(),
            "Restarted TWR shutdown failed"
        );

        handoff.seal();

        check(
            twr.start() == WSAEINVAL,
            "TWR started with a sealed handoff"
        );

        std::cout
            << "[PASS] TWR lifecycle and restart test\n";
    }

    // Tests worker registration and buffered worker messages.
    void testRegistrationAndMessages()
    {
        ReceiverTestServer server;

        TcpClient worker1;
        TcpClient worker2;

        startServer(server);

        connectClient(server, worker1);
        connectClient(server, worker2);

        registerWorker(worker1, 10);

        // Send messages immediately after registration.
        sendHeartbeat(worker1);

        sendResult(worker1, 100, 1);

        registerWorker(worker2, 20);

        sendHeartbeat(worker2);

        sendResult(worker2, 200, 2);

        waitUntil(
            server,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.handedOffWorkers == 2 &&
                    twr.activeWorkers == 2 &&
                    twr.observedHeartbeats == 2 &&
                    twr.observedResults == 2;
            }
        );

        const TwrWorkerInfo first =
            getWorker(server.twr, 10);

        const TwrWorkerInfo second =
            getWorker(server.twr, 20);

        check(
            first.sessionId != second.sessionId,
            "Worker session identifiers are not unique"
        );

        check(
            first.observedHeartbeats == 1 &&
            first.observedResults == 1 &&
            first.lastLbRequestId == 100 &&
            first.lastAttemptId == 1,
            "First worker information is incorrect"
        );

        check(
            second.observedHeartbeats == 1 &&
            second.observedResults == 1 &&
            second.lastLbRequestId == 200 &&
            second.lastAttemptId == 2,
            "Second worker information is incorrect"
        );

        // Ensure the next heartbeat has a later timestamp.
        while (GetTickCount64() <= first.lastHeartbeatAt)
        {
            Sleep(1);
        }

        sendHeartbeat(worker1);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.observedHeartbeats == 3;
            }
        );

        const TwrWorkerInfo updated =
            getWorker(server.twr, 10);

        check(
            updated.lastHeartbeatAt >
            first.lastHeartbeatAt,
            "Heartbeat timestamp was not updated"
        );

        check(
            updated.observedHeartbeats == 2,
            "First worker heartbeat count is incorrect"
        );

        const TwrWorkerInfo unchanged =
            getWorker(server.twr, 20);

        check(
            unchanged.lastHeartbeatAt ==
            second.lastHeartbeatAt,
            "Unrelated worker timestamp was modified"
        );

        stopServer(server);

        expectPeerClosed(worker1);
        expectPeerClosed(worker2);

        checkResult(
            worker1.disconnect(),
            "First worker cleanup failed"
        );

        checkResult(
            worker2.disconnect(),
            "Second worker cleanup failed"
        );

        std::cout
            << "[PASS] TWR registration and message test\n";
    }

    // Tests duplicate identifiers and new worker sessions.
    void testDuplicateAndReconnect()
    {
        ReceiverTestServer server;

        TcpClient original;
        TcpClient duplicate;
        TcpClient replacement;

        startServer(server);

        connectClient(server, original);

        registerWorker(original, 7);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        const TwrWorkerInfo originalInfo =
            getWorker(server.twr, 7);

        connectClient(server, duplicate);

        registerWorker(duplicate, 7);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.rejectedWorkers == 1 &&
                    twr.activeWorkers == 1;
            }
        );

        expectPeerClosed(duplicate);

        check(
            getWorker(
                server.twr, 7
            ).sessionId == originalInfo.sessionId,
            "Duplicate worker replaced the original session"
        );

        // The original worker must remain functional.
        sendHeartbeat(original);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.observedHeartbeats == 1;
            }
        );

        checkResult(
            duplicate.disconnect(),
            "Duplicate worker cleanup failed"
        );

        checkResult(
            original.disconnect(),
            "Original worker cleanup failed"
        );

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 0 &&
                    twr.disconnectedWorkers == 1;
            }
        );

        TwrWorkerInfo removed;

        check(
            !server.twr.getWorker(7, removed),
            "Disconnected worker remained active"
        );

        connectClient(server, replacement);

        registerWorker(replacement, 7);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1 &&
                    twr.registeredWorkers == 2;
            }
        );

        const TwrWorkerInfo newInfo =
            getWorker(server.twr, 7);

        check(
            newInfo.sessionId != originalInfo.sessionId,
            "Replacement worker reused an old session"
        );

        check(
            newInfo.observedHeartbeats == 0 &&
            newInfo.observedResults == 0,
            "Replacement worker inherited old counters"
        );

        stopServer(server);

        checkResult(
            replacement.disconnect(),
            "Replacement worker cleanup failed"
        );

        std::cout
            << "       Previous session: "
            << originalInfo.sessionId << "\n"
            << "       New session: "
            << newInfo.sessionId << "\n";

        std::cout
            << "[PASS] TWR duplicate and reconnect test\n";
    }

    // Tests processing beyond the per-iteration message budget.
    void testBufferedMessages()
    {
        ReceiverTestServer server;

        TcpClient worker;

        startServer(server);

        connectClient(server, worker);

        registerWorker(worker, 42);

        constexpr uint64_t MESSAGE_COUNT = 100;

        for (uint64_t i = 1;
            i <= MESSAGE_COUNT;
            ++i)
        {
            sendHeartbeat(worker);

            sendResult(worker, i, 1);
        }

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1 &&
                    twr.observedHeartbeats ==
                    MESSAGE_COUNT &&
                    twr.observedResults ==
                    MESSAGE_COUNT;
            }
        );

        const TwrWorkerInfo information =
            getWorker(server.twr, 42);

        check(
            information.observedHeartbeats ==
            MESSAGE_COUNT &&
            information.observedResults ==
            MESSAGE_COUNT,
            "Worker messages were lost or duplicated"
        );

        check(
            information.lastLbRequestId ==
            MESSAGE_COUNT &&
            information.lastAttemptId == 1,
            "Final buffered result is incorrect"
        );

        checkResult(
            worker.disconnect(),
            "Buffered worker cleanup failed"
        );

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 0;
            }
        );

        stopServer(server);

        check(
            server.twr.getStatistics().observedResults ==
            MESSAGE_COUNT &&
            server.twr.getStatistics().observedHeartbeats ==
            MESSAGE_COUNT,
            "Unexpected additional worker messages"
        );

        std::cout
            << "       Heartbeats received: "
            << MESSAGE_COUNT << "\n"
            << "       Results received: "
            << MESSAGE_COUNT << "\n";

        std::cout
            << "[PASS] TWR buffered worker messages test\n";
    }

    // Tests worker role protection.
    void testInvalidWorkerMessages()
    {
        ReceiverTestServer server;

        TcpClient worker1;
        TcpClient worker2;

        startServer(server);

        connectClient(server, worker1);

        registerWorker(worker1, 11);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        // A worker must not send user requests.
        sendRequest(worker1, 1);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 0 &&
                    twr.rejectedWorkers == 1;
            }
        );

        expectPeerClosed(worker1);

        checkResult(
            worker1.disconnect(),
            "Invalid worker cleanup failed"
        );

        connectClient(server, worker2);

        registerWorker(worker2, 22);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        // An active worker must not register again.
        registerWorker(worker2, 99);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 0 &&
                    twr.rejectedWorkers == 2;
            }
        );

        expectPeerClosed(worker2);

        checkResult(
            worker2.disconnect(),
            "Repeated registration cleanup failed"
        );

        check(
            server.tur.getStatistics().observedRequests == 0,
            "Worker request reached the user receiver"
        );

        stopServer(server);

        std::cout
            << "[PASS] TWR invalid worker message test\n";
    }

    // Tests simultaneous operation of TUR and TWR.
    void testParallelReceivers()
    {
        ReceiverTestServer server;

        TcpClient user1;
        TcpClient user2;

        TcpClient worker1;
        TcpClient worker2;

        startServer(server);

        connectClient(server, user1);
        connectClient(server, worker1);
        connectClient(server, user2);
        connectClient(server, worker2);

        registerUser(user1);
        registerWorker(worker1, 1);
        registerUser(user2);
        registerWorker(worker2, 2);

        waitUntil(
            server,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.activeUsers == 2 &&
                    twr.activeWorkers == 2;
            }
        );

        constexpr uint64_t ITERATIONS = 50;

        for (uint64_t i = 1;
            i <= ITERATIONS;
            ++i)
        {
            sendRequest(user1, i);

            sendHeartbeat(worker1);

            sendRequest(user2, i);

            sendResult(worker2, i, 1);
        }

        waitUntil(
            server,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.observedRequests ==
                    ITERATIONS * 2 &&
                    twr.observedHeartbeats ==
                    ITERATIONS &&
                    twr.observedResults ==
                    ITERATIONS;
            }
        );

        const TwrWorkerInfo first =
            getWorker(server.twr, 1);

        const TwrWorkerInfo second =
            getWorker(server.twr, 2);

        check(
            first.observedHeartbeats ==
            ITERATIONS &&
            first.observedResults == 0,
            "Incorrect first worker message ownership"
        );

        check(
            second.observedResults ==
            ITERATIONS &&
            second.observedHeartbeats == 0,
            "Incorrect second worker message ownership"
        );

        // Stop TUR while TWR remains active.
        checkResult(
            server.tur.stop(),
            "TUR shutdown failed"
        );

        expectPeerClosed(user1);
        expectPeerClosed(user2);

        // Worker connections must remain functional.
        sendHeartbeat(worker1);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 2 &&
                    twr.observedHeartbeats ==
                    ITERATIONS + 1;
            }
        );

        // Stop TWR after the producer has stopped.
        server.handoff.seal();

        checkResult(
            server.twr.stop(),
            "TWR shutdown failed"
        );

        expectPeerClosed(worker1);
        expectPeerClosed(worker2);

        check(
            server.handoff.isEmpty(),
            "Handoff queue retained connections"
        );

        check(
            server.twr.getStatistics().activeWorkers == 0,
            "TWR retained worker connections"
        );

        checkResult(
            user1.disconnect(),
            "First user cleanup failed"
        );

        checkResult(
            user2.disconnect(),
            "Second user cleanup failed"
        );

        checkResult(
            worker1.disconnect(),
            "First worker cleanup failed"
        );

        checkResult(
            worker2.disconnect(),
            "Second worker cleanup failed"
        );

        std::cout
            << "       User requests: 100\n"
            << "       Worker heartbeats: 51\n"
            << "       Worker results: 50\n";

        std::cout
            << "[PASS] Parallel TUR and TWR test\n";
    }

    // Tests cleanup of connections not yet adopted by TWR.
    void testPendingHandoffCleanup()
    {
        ReceiverTestServer server;

        TcpClient worker;

        // Start only TUR, leaving TWR inactive.
        checkResult(
            server.tur.start(
                LOCAL_ADDRESS, 0),
            "TUR startup failed"
        );

        connectClient(server, worker);

        registerWorker(worker, 55);

        waitUntil(
            server,
            [](const TurStatistics& tur,
                const TwrStatistics&)
            {
                return tur.handedOffWorkers == 1;
            }
        );

        check(
            server.handoff.size() == 1,
            "Pending worker connection is missing"
        );

        checkResult(
            server.tur.stop(),
            "TUR shutdown failed"
        );

        server.handoff.seal();

        checkResult(
            server.twr.stop(),
            "Pending handoff cleanup failed"
        );

        check(
            server.handoff.isEmpty(),
            "Pending worker connection was not released"
        );

        expectPeerClosed(worker);

        checkResult(
            worker.disconnect(),
            "Pending worker client cleanup failed"
        );

        std::cout
            << "[PASS] TWR pending handoff cleanup test\n";
    }
}

// Runs all TWR receiver tests.
void runTwrReceiverTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for TWR tests failed"
    );

    testLifecycle();

    testRegistrationAndMessages();

    testDuplicateAndReconnect();

    testBufferedMessages();

    testInvalidWorkerMessages();

    testParallelReceivers();

    testPendingHandoffCleanup();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after TWR tests failed"
    );

    std::cout
        << "All TWR receiver tests passed.\n";
}