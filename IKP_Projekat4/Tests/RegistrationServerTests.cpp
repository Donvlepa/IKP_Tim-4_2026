#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>

#include "WinsockContext.h"
#include "RegistrationServer.h"
#include "TcpClient.h"
#include "MessageIO.h"
#include "Payloads.h"
#include "TestSuites.h"

namespace
{
    constexpr const char* LOCAL_ADDRESS =
        "127.0.0.1";

    constexpr uint32_t TEST_TIMEOUT_MS = 3000;

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

    // Checks a Winsock operation result.
    void checkResult(int result, const char* message)
    {
        if (result != 0)
        {
            std::cerr
                << "[FAIL] " << message
                << " | Winsock error: "
                << result << "\n";

            std::abort();
        }
    }

    // Starts a test registration server.
    void startServer(RegistrationServer& server)
    {
        checkResult(
            server.start(LOCAL_ADDRESS, 0),
            "Registration server startup failed"
        );

        check(
            server.isRunning(),
            "Registration server is inactive"
        );

        check(
            server.getPort() != 0,
            "Registration server port is invalid"
        );
    }

    // Polls the server until a condition becomes true.
    template<typename Predicate>
    void pollUntil(
        RegistrationServer& server,
        Predicate predicate
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (!predicate())
        {
            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Registration server polling timed out"
            );

            checkResult(
                server.poll(10),
                "Registration server polling failed"
            );
        }
    }

    // Connects a client and waits for TCP acceptance.
    void connectClient(
        RegistrationServer& server,
        TcpClient& client
    )
    {
        const std::size_t expectedCount =
            server.getConnectionCount() + 1;

        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                server.getPort()),
            "Test client connection failed"
        );

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() ==
                    expectedCount;
            }
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

    // Sends a test user request.
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
            static_cast<uint32_t>(sizeof(data));

        request.data = data;

        sendChecked(client, request);
    }

    // Closes the client and waits for server cleanup.
    void disconnectClient(
        RegistrationServer& server,
        TcpClient& client
    )
    {
        const std::size_t currentCount =
            server.getConnectionCount();

        check(
            currentCount > 0,
            "No server connection to disconnect"
        );

        const std::size_t expectedCount =
            currentCount - 1;

        checkResult(
            client.disconnect(),
            "Test client disconnection failed"
        );

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() ==
                    expectedCount;
            }
        );
    }

    // Stops the server and checks its final state.
    void stopServer(RegistrationServer& server)
    {
        checkResult(
            server.stop(),
            "Registration server cleanup failed"
        );

        check(
            !server.isRunning(),
            "Registration server is still running"
        );

        check(
            server.getConnectionCount() == 0,
            "Server contains active connections"
        );

        check(
            server.getUserCount() == 0,
            "Server contains registered users"
        );

        check(
            server.getWorkerCount() == 0,
            "Server contains registered workers"
        );
    }

    // Tests the initial server state.
    void testInitialState()
    {
        RegistrationServer server;

        check(
            !server.isRunning(),
            "Initial server state is invalid"
        );

        check(
            server.getPort() == 0,
            "Inactive server contains a port"
        );

        check(
            server.getObservedRequestCount() == 0,
            "Initial request counter is invalid"
        );

        check(
            server.poll(0) == WSAEINVAL,
            "Inactive server accepted a poll operation"
        );

        stopServer(server);

        std::cout
            << "[PASS] Initial registration server state test\n";
    }

    // Tests registration of multiple users and workers.
    void testMixedRegistration()
    {
        RegistrationServer server;

        TcpClient user1;
        TcpClient user2;
        TcpClient worker1;
        TcpClient worker2;

        startServer(server);

        connectClient(server, user1);
        connectClient(server, worker1);
        connectClient(server, user2);
        connectClient(server, worker2);

        check(
            server.getConnectionCount() == 4,
            "Incorrect initial connection count"
        );

        check(
            server.getUserCount() == 0 &&
            server.getWorkerCount() == 0,
            "Pending connections were registered automatically"
        );

        registerUser(user1);
        registerWorker(worker1, 10);
        registerUser(user2);
        registerWorker(worker2, 20);

        pollUntil(
            server,
            [&]()
            {
                return server.getUserCount() == 2 &&
                    server.getWorkerCount() == 2;
            }
        );

        const uint64_t firstSession =
            server.getWorkerSession(10);

        const uint64_t secondSession =
            server.getWorkerSession(20);

        check(
            firstSession != 0 &&
            secondSession != 0 &&
            firstSession != secondSession,
            "Worker sessions are not unique"
        );

        // Different users may use the same client request ID.
        sendRequest(user1, 1);
        sendRequest(user2, 1);

        pollUntil(
            server,
            [&]()
            {
                return server.getObservedRequestCount() == 2;
            }
        );

        disconnectClient(server, user1);
        disconnectClient(server, worker1);

        check(
            server.getUserCount() == 1 &&
            server.getWorkerCount() == 1,
            "Incorrect state after disconnection"
        );

        check(
            server.getWorkerSession(10) == 0,
            "Disconnected worker session still exists"
        );

        check(
            server.getWorkerSession(20) == secondSession,
            "Unrelated worker session was modified"
        );

        disconnectClient(server, user2);
        disconnectClient(server, worker2);

        stopServer(server);

        std::cout
            << "[PASS] Mixed user and worker registration test\n";
    }

    // Tests registration followed immediately by requests.
    void testRegistrationAndRequests()
    {
        RegistrationServer server;
        TcpClient client;

        startServer(server);

        connectClient(server, client);

        registerUser(client);

        // Exceeds the per-connection message processing budget.
        for (uint64_t i = 1; i <= 40; ++i)
        {
            sendRequest(client, i);
        }

        pollUntil(
            server,
            [&]()
            {
                return server.getUserCount() == 1 &&
                    server.getObservedRequestCount() == 40;
            }
        );

        checkResult(
            server.poll(0),
            "Final registration server polling failed"
        );

        check(
            server.getObservedRequestCount() == 40,
            "Unexpected duplicate request observation"
        );

        disconnectClient(server, client);

        stopServer(server);

        std::cout
            << "[PASS] Registration and buffered requests test\n";
    }

    // Tests duplicate worker rejection and reconnection.
    void testDuplicateWorker()
    {
        RegistrationServer server;

        TcpClient original;
        TcpClient duplicate;
        TcpClient replacement;

        startServer(server);

        connectClient(server, original);

        registerWorker(original, 7);

        pollUntil(
            server,
            [&]()
            {
                return server.getWorkerCount() == 1;
            }
        );

        const uint64_t originalSession =
            server.getWorkerSession(7);

        check(
            originalSession != 0,
            "Original worker session is invalid"
        );

        connectClient(server, duplicate);

        registerWorker(duplicate, 7);

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 1;
            }
        );

        check(
            server.getWorkerCount() == 1,
            "Duplicate worker changed the worker count"
        );

        check(
            server.getWorkerSession(7) == originalSession,
            "Duplicate worker replaced the original session"
        );

        checkResult(
            duplicate.disconnect(),
            "Duplicate client cleanup failed"
        );

        disconnectClient(server, original);

        check(
            server.getWorkerSession(7) == 0,
            "Disconnected session was not removed"
        );

        connectClient(server, replacement);

        registerWorker(replacement, 7);

        pollUntil(
            server,
            [&]()
            {
                return server.getWorkerCount() == 1;
            }
        );

        const uint64_t newSession =
            server.getWorkerSession(7);

        check(
            newSession != 0 &&
            newSession != originalSession,
            "Worker reconnection reused an old session"
        );

        disconnectClient(server, replacement);

        stopServer(server);

        std::cout
            << "[PASS] Duplicate worker and new session test\n";
    }

    // Tests rejection of an invalid first message.
    void testInvalidFirstMessage()
    {
        RegistrationServer server;
        TcpClient client;

        startServer(server);

        connectClient(server, client);

        // Send a request without prior user registration.
        sendRequest(client, 1);

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 0;
            }
        );

        check(
            server.getUserCount() == 0 &&
            server.getWorkerCount() == 0,
            "Invalid first message registered a client"
        );

        check(
            server.getObservedRequestCount() == 0,
            "Unregistered request was observed"
        );

        checkResult(
            client.disconnect(),
            "Invalid client cleanup failed"
        );

        stopServer(server);

        std::cout
            << "[PASS] Invalid first message rejection test\n";
    }

    // Tests rejection of worker ID zero.
    void testInvalidWorkerId()
    {
        RegistrationServer server;
        TcpClient client;

        startServer(server);

        connectClient(server, client);

        registerWorker(client, 0);

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 0;
            }
        );

        check(
            server.getWorkerCount() == 0,
            "Invalid worker identifier was accepted"
        );

        checkResult(
            client.disconnect(),
            "Invalid worker cleanup failed"
        );

        stopServer(server);

        std::cout
            << "[PASS] Invalid worker ID rejection test\n";
    }

    // Tests messages that do not match the registered role.
    void testRoleProtection()
    {
        RegistrationServer server;

        TcpClient user;
        TcpClient worker;

        startServer(server);

        connectClient(server, user);

        registerUser(user);

        pollUntil(
            server,
            [&]()
            {
                return server.getUserCount() == 1;
            }
        );

        HeartbeatPayload heartbeat;

        // A user must not send worker heartbeat messages.
        sendChecked(user, heartbeat);

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 0;
            }
        );

        checkResult(
            user.disconnect(),
            "Rejected user cleanup failed"
        );

        connectClient(server, worker);

        registerWorker(worker, 5);

        pollUntil(
            server,
            [&]()
            {
                return server.getWorkerCount() == 1;
            }
        );

        // A worker must not send user requests.
        sendRequest(worker, 1);

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 0;
            }
        );

        check(
            server.getObservedRequestCount() == 0,
            "Worker request was treated as a user request"
        );

        checkResult(
            worker.disconnect(),
            "Rejected worker cleanup failed"
        );

        stopServer(server);

        std::cout
            << "[PASS] Registered connection role protection test\n";
    }

    // Tests rejection of a repeated registration.
    void testRepeatedRegistration()
    {
        RegistrationServer server;
        TcpClient client;

        startServer(server);

        connectClient(server, client);

        registerUser(client);

        pollUntil(
            server,
            [&]()
            {
                return server.getUserCount() == 1;
            }
        );

        // A registered connection must not change its role.
        registerWorker(client, 99);

        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 0;
            }
        );

        check(
            server.getWorkerCount() == 0,
            "Repeated registration changed the connection role"
        );

        checkResult(
            client.disconnect(),
            "Repeated registration client cleanup failed"
        );

        stopServer(server);

        std::cout
            << "[PASS] Repeated registration rejection test\n";
    }

    // Tests timeout of an unregistered connection.
    void testRegistrationTimeout()
    {
        RegistrationServer server(500);

        TcpClient client;

        startServer(server);

        connectClient(server, client);

        // Do not send a registration message.
        pollUntil(
            server,
            [&]()
            {
                return server.getConnectionCount() == 0;
            }
        );

        check(
            server.getUserCount() == 0 &&
            server.getWorkerCount() == 0,
            "Timed-out connection was registered"
        );

        checkResult(
            client.disconnect(),
            "Timed-out client cleanup failed"
        );

        stopServer(server);

        std::cout
            << "[PASS] Registration timeout test\n";
    }

    // Tests server shutdown with active connections.
    void testShutdown()
    {
        RegistrationServer server;

        TcpClient user;
        TcpClient worker;

        startServer(server);

        connectClient(server, user);
        connectClient(server, worker);

        registerUser(user);
        registerWorker(worker, 15);

        pollUntil(
            server,
            [&]()
            {
                return server.getUserCount() == 1 &&
                    server.getWorkerCount() == 1;
            }
        );

        // Shut down while both clients are active.
        stopServer(server);

        check(
            server.getWorkerSession(15) == 0,
            "Worker session survived server shutdown"
        );

        checkResult(
            user.disconnect(),
            "User socket cleanup failed"
        );

        checkResult(
            worker.disconnect(),
            "Worker socket cleanup failed"
        );

        // Verify that the server can be restarted.
        startServer(server);

        check(
            server.getObservedRequestCount() == 0,
            "Server request counter was not reset"
        );

        stopServer(server);

        std::cout
            << "[PASS] Registration server shutdown test\n";
    }
}

// Runs all registration server tests.
void runRegistrationServerTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for registration tests failed"
    );

    testInitialState();
    testMixedRegistration();
    testRegistrationAndRequests();
    testDuplicateWorker();
    testInvalidFirstMessage();
    testInvalidWorkerId();
    testRoleProtection();
    testRepeatedRegistration();
    testRegistrationTimeout();
    testShutdown();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after registration tests failed"
    );

    std::cout
        << "All registration server tests passed.\n";
}