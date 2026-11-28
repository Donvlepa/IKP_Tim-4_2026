#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "ConnectionContext.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "MessageIO.h"
#include "PayloadCodec.h"
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

    // Starts the TUR receiver on an automatically assigned port.
    void startReceiver(TurReceiver& tur)
    {
        checkResult(
            tur.start(LOCAL_ADDRESS, 0),
            "TUR startup failed"
        );

        check(
            tur.getPort() != 0,
            "TUR listening port is invalid"
        );
    }

    // Waits for a condition while TUR runs independently.
    template<typename Predicate>
    void waitUntil(
        TurReceiver& tur,
        Predicate predicate
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            const TurStatistics statistics =
                tur.getStatistics();

            checkResult(
                statistics.lastError,
                "TUR reported an internal error"
            );

            if (predicate(statistics))
            {
                return;
            }

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "TUR condition timed out"
            );

            Sleep(5);
        }
    }

    // Connects a test client to TUR.
    void connectClient(
        TurReceiver& tur,
        TcpClient& client
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                tur.getPort()),
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
            "No protocol bytes were sent"
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

    // Sends a user request.
    void sendRequest(
        TcpClient& client,
        uint64_t requestId,
        Priority priority = Priority::HIGH
    )
    {
        const uint8_t data[] =
        {
            10, 20, 30
        };

        RequestPayload request;

        request.clientRequestId = requestId;

        request.priority = priority;

        request.dataSize =
            static_cast<uint32_t>(sizeof(data));

        request.data = data;

        sendChecked(client, request);
    }

    // Receives a message or a terminal reception status.
    ReceiveStatus waitForReceive(
        MessageReceiver& receiver,
        SOCKET socketHandle
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            int socketError = 0;

            const ReceiveStatus status =
                receiver.receive(
                    socketHandle,
                    socketError
                );

            if (status == ReceiveStatus::SocketError)
            {
                checkResult(
                    socketError,
                    "Test socket reception failed"
                );
            }

            if (status != ReceiveStatus::WouldBlock)
            {
                return status;
            }

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Test message reception timed out"
            );

            Sleep(5);
        }
    }

    // Verifies heartbeat reception after connection handoff.
    void receiveHeartbeat(
        ConnectionContext& connection
    )
    {
        check(
            waitForReceive(
                connection.receiver,
                connection.getSocket()
            ) == ReceiveStatus::MessageReady,
            "Handed-off heartbeat was not received"
        );

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        check(
            connection.receiver.getMessage(
                header,
                payload),
            "Heartbeat retrieval failed"
        );

        check(
            header.type == MessageType::HEARTBEAT,
            "Incorrect message after TUR handoff"
        );

        HeartbeatPayload heartbeat;

        check(
            PayloadCodec::deserializeHeartbeat(
                payload,
                header.length,
                heartbeat),
            "Handed-off heartbeat is invalid"
        );

        check(
            connection.receiver.consumeMessage(),
            "Heartbeat consumption failed"
        );
    }

    // Verifies that a peer closed its TCP connection.
    void expectPeerClosed(TcpClient& client)
    {
        MessageReceiver receiver;

        check(
            waitForReceive(
                receiver,
                client.getSocket()
            ) == ReceiveStatus::ConnectionClosed,
            "Peer did not close the TCP connection"
        );
    }

    // Stops TUR and checks that local connections were released.
    void stopReceiver(TurReceiver& tur)
    {
        checkResult(
            tur.stop(),
            "TUR shutdown failed"
        );

        const TurStatistics statistics =
            tur.getStatistics();

        check(
            statistics.pendingConnections == 0,
            "TUR retained pending connections"
        );

        check(
            statistics.activeUsers == 0,
            "TUR retained user connections"
        );

        check(
            tur.getPort() == 0,
            "TUR retained its listening port"
        );
    }

    // Tests initial state, duplicate startup and restart.
    void testLifecycle()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        const TurStatistics initial =
            tur.getStatistics();

        check(
            initial.pendingConnections == 0 &&
            initial.activeUsers == 0 &&
            initial.handedOffWorkers == 0 &&
            initial.observedRequests == 0 &&
            initial.rejectedConnections == 0 &&
            initial.lastError == 0,
            "Incorrect initial TUR statistics"
        );

        check(
            tur.getPort() == 0,
            "Inactive TUR contains a listening port"
        );

        checkResult(
            tur.stop(),
            "Inactive TUR cleanup failed"
        );

        startReceiver(tur);

        const uint16_t originalPort =
            tur.getPort();

        check(
            tur.start(LOCAL_ADDRESS, 0)
            == WSAEINVAL,
            "Duplicate TUR startup was accepted"
        );

        check(
            tur.getPort() == originalPort,
            "Duplicate startup changed the listening port"
        );

        stopReceiver(tur);

        startReceiver(tur);

        stopReceiver(tur);

        checkResult(
            tur.stop(),
            "Repeated TUR cleanup failed"
        );

        std::cout
            << "[PASS] TUR lifecycle and restart test\n";
    }

    // Tests registration and reception from multiple users.
    void testMultipleUsers()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        TcpClient user1;
        TcpClient user2;

        startReceiver(tur);

        connectClient(tur, user1);
        connectClient(tur, user2);

        registerUser(user1);
        registerUser(user2);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 2;
            }
        );

        // Different users may use the same client request ID.
        sendRequest(user1, 1, Priority::HIGH);
        sendRequest(user2, 1, Priority::LOW);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.observedRequests == 2;
            }
        );

        check(
            handoff.isEmpty(),
            "A user connection entered the worker handoff"
        );

        checkResult(
            user1.disconnect(),
            "First user disconnection failed"
        );

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1;
            }
        );

        // Verify that the remaining user can still send requests.
        sendRequest(user2, 2, Priority::MEDIUM);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.observedRequests == 3;
            }
        );

        checkResult(
            user2.disconnect(),
            "Second user disconnection failed"
        );

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 0;
            }
        );

        stopReceiver(tur);

        std::cout
            << "[PASS] TUR multiple user reception test\n";
    }

    // Tests continuous reception beyond the polling budget.
    void testBufferedRequests()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        TcpClient user;

        startReceiver(tur);

        connectClient(tur, user);

        registerUser(user);

        constexpr uint64_t REQUEST_COUNT = 100;

        // Send requests without waiting for TUR between sends.
        for (uint64_t i = 1;
            i <= REQUEST_COUNT;
            ++i)
        {
            sendRequest(
                user,
                i,
                Priority::MEDIUM
            );
        }

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.observedRequests ==
                    REQUEST_COUNT;
            }
        );

        checkResult(
            user.disconnect(),
            "Buffered request client cleanup failed"
        );

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 0;
            }
        );

        stopReceiver(tur);

        check(
            tur.getStatistics().observedRequests ==
            REQUEST_COUNT,
            "Buffered requests were lost or duplicated"
        );

        std::cout
            << "       Observed requests: "
            << REQUEST_COUNT << "\n";

        std::cout
            << "[PASS] TUR buffered requests test\n";
    }

    // Tests worker transfer and preservation of receive state.
    void testWorkerHandoff()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        TcpClient worker;

        startReceiver(tur);

        connectClient(tur, worker);

        registerWorker(worker, 42);

        // Send the next message immediately after registration.
        HeartbeatPayload heartbeat;

        sendChecked(worker, heartbeat);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.handedOffWorkers == 1;
            }
        );

        ConnectionContext* connection =
            handoff.tryPop();

        if (connection == nullptr)
        {
            std::cerr
                << "[FAIL] Worker handoff is missing\n";

            std::abort();
        }

        check(
            connection->role == ConnectionRole::Worker,
            "Incorrect handed-off connection role"
        );

        check(
            connection->workerId == 42,
            "Incorrect handed-off worker ID"
        );

        check(
            connection->sessionId == 0,
            "TUR assigned a worker session prematurely"
        );

        check(
            connection->needsDrain,
            "Handed-off receiver was not marked for draining"
        );

        check(
            connection->hasSocket(),
            "Handed-off worker socket is invalid"
        );

        check(
            tur.getStatistics().pendingConnections == 0,
            "TUR retained a handed-off pending connection"
        );

        check(
            tur.getStatistics().activeUsers == 0,
            "Worker was registered as a user"
        );

        // The main test thread simulates TWR.
        receiveHeartbeat(*connection);

        // Verify another heartbeat on the transferred socket.
        sendChecked(worker, heartbeat);

        receiveHeartbeat(*connection);

        checkResult(
            connection->close(),
            "Handed-off worker socket cleanup failed"
        );

        delete connection;

        expectPeerClosed(worker);

        checkResult(
            worker.disconnect(),
            "Worker client cleanup failed"
        );

        stopReceiver(tur);

        std::cout
            << "[PASS] TUR worker connection handoff test\n";
    }

    // Tests rejection of invalid registrations and role violations.
    void testInvalidMessages()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        TcpClient unregistered;
        TcpClient invalidWorker;
        TcpClient invalidUser;

        startReceiver(tur);

        // STEP 1: A request without registration.
        connectClient(tur, unregistered);

        sendRequest(unregistered, 1);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 1;
            }
        );

        checkResult(
            unregistered.disconnect(),
            "Unregistered client cleanup failed"
        );

        // STEP 2: Invalid worker identifier.
        connectClient(tur, invalidWorker);

        registerWorker(invalidWorker, 0);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 2;
            }
        );

        checkResult(
            invalidWorker.disconnect(),
            "Invalid worker cleanup failed"
        );

        // STEP 3: A user sending a worker message.
        connectClient(tur, invalidUser);

        registerUser(invalidUser);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1;
            }
        );

        HeartbeatPayload heartbeat;

        sendChecked(invalidUser, heartbeat);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 3 &&
                    statistics.activeUsers == 0;
            }
        );

        checkResult(
            invalidUser.disconnect(),
            "Invalid user cleanup failed"
        );

        check(
            tur.getStatistics().observedRequests == 0,
            "Invalid messages were counted as requests"
        );

        check(
            handoff.isEmpty(),
            "Invalid worker entered the handoff queue"
        );

        stopReceiver(tur);

        std::cout
            << "[PASS] TUR invalid message rejection test\n";
    }

    // Tests rejection after the registration timeout.
    void testRegistrationTimeout()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff, 500);

        TcpClient client;

        startReceiver(tur);

        connectClient(tur, client);

        // Do not send any registration message.
        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 1 &&
                    statistics.pendingConnections == 0;
            }
        );

        expectPeerClosed(client);

        checkResult(
            client.disconnect(),
            "Timed-out client cleanup failed"
        );

        stopReceiver(tur);

        std::cout
            << "[PASS] TUR registration timeout test\n";
    }

    // Tests behavior when worker transfers are no longer accepted.
    void testSealedHandoff()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        TcpClient worker;

        startReceiver(tur);

        handoff.seal();

        connectClient(tur, worker);

        registerWorker(worker, 15);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 1 &&
                    statistics.pendingConnections == 0;
            }
        );

        check(
            tur.getStatistics().handedOffWorkers == 0,
            "Worker was transferred to a sealed queue"
        );

        check(
            handoff.isEmpty(),
            "Sealed handoff contains a rejected connection"
        );

        checkResult(
            worker.disconnect(),
            "Rejected worker cleanup failed"
        );

        stopReceiver(tur);

        std::cout
            << "[PASS] TUR sealed handoff rejection test\n";
    }

    // Tests shutdown and exclusive connection ownership.
    void testShutdownOwnership()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);

        TcpClient user;
        TcpClient worker;
        TcpClient pending;

        startReceiver(tur);

        connectClient(tur, user);
        connectClient(tur, worker);
        connectClient(tur, pending);

        registerUser(user);
        registerWorker(worker, 7);

        waitUntil(
            tur,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.pendingConnections == 1 &&
                    statistics.handedOffWorkers == 1;
            }
        );

        // Stop TUR while all three clients are active.
        stopReceiver(tur);

        // TUR must close only its own connections.
        expectPeerClosed(user);

        expectPeerClosed(pending);

        check(
            handoff.size() == 1,
            "TUR removed a transferred worker during shutdown"
        );

        ConnectionContext* connection =
            handoff.tryPop();

        if (connection == nullptr)
        {
            std::cerr
                << "[FAIL] Transferred worker is missing\n";

            std::abort();
        }

        // The worker socket must remain usable after TUR stops.
        HeartbeatPayload heartbeat;

        sendChecked(worker, heartbeat);

        receiveHeartbeat(*connection);

        checkResult(
            connection->close(),
            "Transferred worker cleanup failed"
        );

        delete connection;

        expectPeerClosed(worker);

        checkResult(
            user.disconnect(),
            "User cleanup failed"
        );

        checkResult(
            worker.disconnect(),
            "Worker cleanup failed"
        );

        checkResult(
            pending.disconnect(),
            "Pending client cleanup failed"
        );

        std::cout
            << "[PASS] TUR shutdown ownership test\n";
    }
}

// Runs all TUR receiver tests.
void runTurReceiverTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for TUR tests failed"
    );

    testLifecycle();
    testMultipleUsers();
    testBufferedRequests();
    testWorkerHandoff();
    testInvalidMessages();
    testRegistrationTimeout();
    testSealedHandoff();
    testShutdownOwnership();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after TUR tests failed"
    );

    std::cout
        << "All TUR receiver tests passed.\n";
}