#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "RequestRegistry.h"
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
            std::cerr << "[FAIL] " << message << "\n";

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
                << " | Error: " << result << "\n";

            std::abort();
        }
    }

    // Owns the request registry and receiver.
    struct TurRequestTestServer
    {
        RequestRegistry registry;

        ConnectionHandoff handoff;

        TurReceiver tur;

        explicit TurRequestTestServer(
            std::size_t capacity =
            RequestRegistry::MAX_REQUESTS
        )
            : registry(capacity),
            tur(handoff, registry)
        {
        }
    };

    // Starts a test server.
    void startServer(TurRequestTestServer& server)
    {
        check(
            server.registry.isValid(),
            "Request registry is invalid"
        );

        checkResult(
            server.tur.start(LOCAL_ADDRESS, 0),
            "TUR startup failed"
        );

        check(
            server.tur.getPort() != 0,
            "Invalid listening port"
        );
    }

    // Stops the receiver before destroying the registry.
    void stopServer(TurRequestTestServer& server)
    {
        checkResult(
            server.tur.stop(),
            "TUR shutdown failed"
        );

        const TurStatistics statistics =
            server.tur.getStatistics();

        check(
            statistics.activeUsers == 0 &&
            statistics.pendingConnections == 0,
            "TUR retained active connections"
        );
    }

    // Waits for a synchronized receiver condition.
    template<typename Predicate>
    void waitUntil(
        TurRequestTestServer& server,
        Predicate predicate
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            const TurStatistics statistics =
                server.tur.getStatistics();

            checkResult(
                statistics.lastError,
                "TUR reported an internal error"
            );

            checkResult(
                server.registry.getFatalError(),
                "Request registry reported an internal error"
            );

            if (predicate(statistics))
            {
                return;
            }

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Integration test timed out"
            );

            Sleep(5);
        }
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
            "Protocol message sending failed"
        );

        check(
            bytesSent > 0,
            "No message bytes were sent"
        );
    }

    // Connects a client to the receiver.
    void connectClient(
        TurRequestTestServer& server,
        TcpClient& client
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                server.tur.getPort()),
            "Client connection failed"
        );
    }

    // Connects and registers a user.
    void connectUser(
        TurRequestTestServer& server,
        TcpClient& client
    )
    {
        connectClient(server, client);

        RegisterUrPayload registration;

        sendChecked(client, registration);
    }

    // Sends a request with caller-provided data.
    void sendRequest(
        TcpClient& client,
        uint64_t clientRequestId,
        Priority priority,
        const uint8_t* data,
        uint32_t dataSize
    )
    {
        RequestPayload request;

        request.clientRequestId = clientRequestId;

        request.priority = priority;

        request.data = data;

        request.dataSize = dataSize;

        sendChecked(client, request);
    }

    // Retrieves registered request metadata.
    RegisteredRequestInfo getRequest(
        RequestRegistry& registry,
        uint64_t lbRequestId
    )
    {
        RegisteredRequestInfo information;

        check(
            registry.getRequest(
                lbRequestId,
                information),
            "Registered request was not found"
        );

        check(
            information.lbRequestId == lbRequestId,
            "Incorrect internal request identifier"
        );

        return information;
    }

    // Verifies priority queue sizes.
    void checkQueueSizes(
        RequestRegistry& registry,
        std::size_t high,
        std::size_t medium,
        std::size_t low
    )
    {
        check(
            registry.getQueueSize(
                Priority::HIGH) == high,
            "Incorrect high priority queue size"
        );

        check(
            registry.getQueueSize(
                Priority::MEDIUM) == medium,
            "Incorrect medium priority queue size"
        );

        check(
            registry.getQueueSize(
                Priority::LOW) == low,
            "Incorrect low priority queue size"
        );

        check(
            registry.size() == high + medium + low,
            "Incorrect registry size"
        );
    }

    // Checks and restores semaphore signals.
    // No request producers or consumers may be active.
    void checkSemaphoreCount(
        RequestRegistry& registry,
        LONG expectedCount
    )
    {
        HANDLE semaphore =
            registry.getRequestsSemaphore();

        check(
            semaphore != nullptr,
            "Invalid request semaphore"
        );

        for (LONG i = 0; i < expectedCount; ++i)
        {
            check(
                WaitForSingleObject(
                    semaphore, 0
                ) == WAIT_OBJECT_0,
                "Missing request semaphore signal"
            );
        }

        check(
            WaitForSingleObject(
                semaphore, 0
            ) == WAIT_TIMEOUT,
            "Unexpected request semaphore signal"
        );

        if (expectedCount > 0)
        {
            check(
                ReleaseSemaphore(
                    semaphore,
                    expectedCount,
                    nullptr) != 0,
                "Semaphore restoration failed"
            );
        }
    }

    // Verifies stored request bytes.
    void checkData(
        RequestRegistry& registry,
        uint64_t lbRequestId,
        const uint8_t* expected,
        uint32_t expectedSize
    )
    {
        uint8_t output[32] = {};

        uint32_t copied = 0;

        check(
            expectedSize <= sizeof(output),
            "Test output buffer is too small"
        );

        check(
            registry.copyRequestData(
                lbRequestId,
                output,
                sizeof(output),
                copied),
            "Stored data retrieval failed"
        );

        check(
            copied == expectedSize,
            "Incorrect stored data size"
        );

        check(
            std::memcmp(
                output,
                expected,
                expectedSize
            ) == 0,
            "Stored request bytes are incorrect"
        );
    }

    // Tests rejection of an invalid registry.
    void testInvalidRegistry()
    {
        RequestRegistry registry(0);

        ConnectionHandoff handoff;

        TurReceiver tur(handoff, registry);

        check(
            tur.start(
                LOCAL_ADDRESS, 0
            ) == WSAEINVAL,
            "TUR accepted an invalid request registry"
        );

        check(
            tur.getPort() == 0,
            "Invalid startup opened a listener"
        );

        std::cout
            << "[PASS] TUR invalid registry startup test\n";
    }

    // Tests network reception and priority routing.
    void testNetworkRequestRegistration()
    {
        TurRequestTestServer server;

        TcpClient user;

        startServer(server);

        connectUser(server, user);

        const uint8_t highData[] =
        {
            10, 20, 30
        };

        const uint8_t mediumData[] =
        {
            40, 50, 60
        };

        const uint8_t lowData[] =
        {
            70, 80, 90
        };

        sendRequest(
            user, 101, Priority::HIGH,
            highData, sizeof(highData)
        );

        sendRequest(
            user, 102, Priority::MEDIUM,
            mediumData, sizeof(mediumData)
        );

        sendRequest(
            user, 103, Priority::LOW,
            lowData, sizeof(lowData)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.observedRequests == 3;
            }
        );

        checkQueueSizes(
            server.registry, 1, 1, 1
        );

        const RegisteredRequestInfo high =
            getRequest(server.registry, 1);

        const RegisteredRequestInfo medium =
            getRequest(server.registry, 2);

        const RegisteredRequestInfo low =
            getRequest(server.registry, 3);

        check(
            high.clientRequestId == 101 &&
            medium.clientRequestId == 102 &&
            low.clientRequestId == 103,
            "Original request identifiers were lost"
        );

        check(
            high.priority == Priority::HIGH &&
            medium.priority == Priority::MEDIUM &&
            low.priority == Priority::LOW,
            "Incorrect request priority routing"
        );

        check(
            high.userSessionId != 0 &&
            high.userSessionId == medium.userSessionId &&
            high.userSessionId == low.userSessionId,
            "Requests from one user have different sessions"
        );

        check(
            high.userSocket != INVALID_SOCKET &&
            high.userSocket == medium.userSocket &&
            high.userSocket == low.userSocket,
            "Incorrect user socket association"
        );

        check(
            high.status == RequestStatus::QUEUED &&
            medium.status == RequestStatus::QUEUED &&
            low.status == RequestStatus::QUEUED,
            "Incorrect initial request status"
        );

        checkData(
            server.registry,
            1, highData, sizeof(highData)
        );

        checkData(
            server.registry,
            2, mediumData, sizeof(mediumData)
        );

        checkData(
            server.registry,
            3, lowData, sizeof(lowData)
        );

        uint64_t firstId = 0;

        check(
            server.registry.peekNext(
                Priority::HIGH, firstId
            ) && firstId == 1,
            "Incorrect high priority queue head"
        );

        check(
            server.registry.peekNext(
                Priority::MEDIUM, firstId
            ) && firstId == 2,
            "Incorrect medium priority queue head"
        );

        check(
            server.registry.peekNext(
                Priority::LOW, firstId
            ) && firstId == 3,
            "Incorrect low priority queue head"
        );

        checkResult(
            user.disconnect(),
            "User disconnection failed"
        );

        stopServer(server);

        checkSemaphoreCount(
            server.registry, 3
        );

        std::cout
            << "       QH: 1 | QM: 1 | QL: 1\n"
            << "[PASS] TUR network request registration test\n";
    }

    // Tests multiple users and independent session ownership.
    void testMultipleUserSessions()
    {
        TurRequestTestServer server;

        TcpClient firstUser;
        TcpClient secondUser;

        startServer(server);

        connectUser(server, firstUser);
        connectUser(server, secondUser);

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 2;
            }
        );

        const uint8_t data[] = { 1, 2, 3 };

        // Both users use the same original request identifier.
        sendRequest(
            firstUser, 1, Priority::HIGH,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.observedRequests == 1;
            }
        );

        sendRequest(
            secondUser, 1, Priority::LOW,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.observedRequests == 2;
            }
        );

        const RegisteredRequestInfo first =
            getRequest(server.registry, 1);

        const RegisteredRequestInfo second =
            getRequest(server.registry, 2);

        check(
            first.clientRequestId == 1 &&
            second.clientRequestId == 1,
            "Original client request IDs were changed"
        );

        check(
            first.userSessionId != 0 &&
            second.userSessionId != 0 &&
            first.userSessionId != second.userSessionId,
            "Different users share a session"
        );

        check(
            first.userConnected &&
            second.userConnected,
            "Registered users are not connected"
        );

        // Disconnect only the first user.
        checkResult(
            firstUser.disconnect(),
            "First user disconnection failed"
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1;
            }
        );

        const RegisteredRequestInfo oldRequest =
            getRequest(server.registry, 1);

        const RegisteredRequestInfo activeRequest =
            getRequest(server.registry, 2);

        check(
            !oldRequest.userConnected,
            "Disconnected request still has an active user"
        );

        check(
            activeRequest.userConnected,
            "Unrelated user was marked disconnected"
        );

        check(
            oldRequest.status == RequestStatus::QUEUED &&
            !oldRequest.responseSent,
            "Disconnection incorrectly completed the request"
        );

        // A disconnected user's request must remain queued.
        checkQueueSizes(
            server.registry, 1, 0, 1
        );

        checkResult(
            secondUser.disconnect(),
            "Second user disconnection failed"
        );

        stopServer(server);

        checkSemaphoreCount(
            server.registry, 2
        );

        std::cout
            << "[PASS] TUR independent user sessions test\n";
    }

    // Tests a new user session after reconnection.
    void testUserReconnect()
    {
        TurRequestTestServer server;

        TcpClient user;

        startServer(server);

        const uint8_t data[] = { 15, 25 };

        connectUser(server, user);

        sendRequest(
            user, 1, Priority::HIGH,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.observedRequests == 1;
            }
        );

        const RegisteredRequestInfo original =
            getRequest(server.registry, 1);

        checkResult(
            user.disconnect(),
            "Original session disconnection failed"
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 0;
            }
        );

        // Reconnect using the same TcpClient object.
        connectUser(server, user);

        sendRequest(
            user, 1, Priority::MEDIUM,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.observedRequests == 2;
            }
        );

        const RegisteredRequestInfo replacement =
            getRequest(server.registry, 2);

        check(
            original.userSessionId !=
            replacement.userSessionId,
            "User reconnection reused an old session"
        );

        check(
            !getRequest(
                server.registry, 1
            ).userConnected,
            "Old session became connected again"
        );

        check(
            replacement.userConnected,
            "Replacement session is disconnected"
        );

        checkQueueSizes(
            server.registry, 1, 1, 0
        );

        checkResult(
            user.disconnect(),
            "Replacement user cleanup failed"
        );

        stopServer(server);

        checkSemaphoreCount(
            server.registry, 2
        );

        std::cout
            << "       Previous user session: "
            << original.userSessionId << "\n"
            << "       New user session: "
            << replacement.userSessionId << "\n"
            << "[PASS] TUR user reconnection test\n";
    }

    // Tests request storage across receive-buffer reuse.
    void testBufferedRequests()
    {
        TurRequestTestServer server;

        TcpClient user;

        startServer(server);

        connectUser(server, user);

        constexpr uint64_t REQUEST_COUNT = 100;

        const Priority priorities[] =
        {
            Priority::HIGH,
            Priority::MEDIUM,
            Priority::LOW
        };

        for (uint64_t i = 1;
            i <= REQUEST_COUNT;
            ++i)
        {
            const uint8_t data[] =
            {
                static_cast<uint8_t>(i),
                0,
                static_cast<uint8_t>(255 - i)
            };

            sendRequest(
                user,
                i,
                priorities[(i - 1) % 3],
                data,
                sizeof(data)
            );
        }

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.observedRequests ==
                    REQUEST_COUNT;
            }
        );

        // 100 requests: 34 HIGH, 33 MEDIUM, 33 LOW.
        checkQueueSizes(
            server.registry, 34, 33, 33
        );

        const uint64_t sessionId =
            getRequest(
                server.registry, 1
            ).userSessionId;

        for (uint64_t i = 1;
            i <= REQUEST_COUNT;
            ++i)
        {
            const RegisteredRequestInfo information =
                getRequest(server.registry, i);

            check(
                information.clientRequestId == i &&
                information.userSessionId == sessionId &&
                information.priority ==
                priorities[(i - 1) % 3],
                "Buffered request metadata was corrupted"
            );

            const uint8_t expected[] =
            {
                static_cast<uint8_t>(i),
                0,
                static_cast<uint8_t>(255 - i)
            };

            checkData(
                server.registry,
                i,
                expected,
                sizeof(expected)
            );
        }

        checkResult(
            user.disconnect(),
            "Buffered user cleanup failed"
        );

        stopServer(server);

        checkSemaphoreCount(
            server.registry,
            static_cast<LONG>(REQUEST_COUNT)
        );

        for (uint64_t i = 1;
            i <= REQUEST_COUNT;
            ++i)
        {
            const RegisteredRequestInfo information =
                getRequest(server.registry, i);

            check(
                !information.userConnected &&
                information.status == RequestStatus::QUEUED,
                "Disconnected buffered request state is incorrect"
            );
        }

        std::cout
            << "       Stored requests: 100\n"
            << "       Verified payloads: 100\n"
            << "       Semaphore signals: 100\n"
            << "[PASS] TUR buffered request storage test\n";
    }

    // Tests rejection of a request without registration.
    void testUnregisteredRequest()
    {
        TurRequestTestServer server;

        TcpClient client;

        startServer(server);

        connectClient(server, client);

        const uint8_t data[] = { 1, 2 };

        sendRequest(
            client, 1, Priority::HIGH,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 1;
            }
        );

        check(
            server.tur.getStatistics().observedRequests == 0,
            "Unregistered request was counted"
        );

        checkResult(
            client.disconnect(),
            "Rejected client cleanup failed"
        );

        stopServer(server);

        checkQueueSizes(
            server.registry, 0, 0, 0
        );

        checkSemaphoreCount(
            server.registry, 0
        );

        std::cout
            << "[PASS] TUR unregistered request rejection test\n";
    }

    // Tests behavior when the registry is full.
    void testRegistryCapacity()
    {
        TurRequestTestServer server(2);

        TcpClient user;

        startServer(server);

        connectUser(server, user);

        const uint8_t data[] = { 10, 20 };

        sendRequest(
            user, 1, Priority::HIGH,
            data, sizeof(data)
        );

        sendRequest(
            user, 2, Priority::LOW,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.observedRequests == 2;
            }
        );

        // The third request exceeds the registry capacity.
        sendRequest(
            user, 3, Priority::MEDIUM,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.rejectedConnections == 1 &&
                    statistics.activeUsers == 0;
            }
        );

        check(
            server.registry.isValid(),
            "Capacity rejection corrupted the registry"
        );

        check(
            server.tur.getStatistics().observedRequests == 2,
            "Rejected request was counted as accepted"
        );

        checkQueueSizes(
            server.registry, 1, 0, 1
        );

        RegisteredRequestInfo missing;

        check(
            !server.registry.getRequest(3, missing),
            "Rejected request exists in the registry"
        );

        check(
            !getRequest(
                server.registry, 1
            ).userConnected &&
            !getRequest(
                server.registry, 2
            ).userConnected,
            "Rejected connection remained marked active"
        );

        checkResult(
            user.disconnect(),
            "Full registry client cleanup failed"
        );

        stopServer(server);

        checkSemaphoreCount(
            server.registry, 2
        );

        std::cout
            << "[PASS] TUR registry capacity rejection test\n";
    }

    // Tests shutdown while a user still has queued requests.
    void testShutdownAndRestart()
    {
        TurRequestTestServer server;

        TcpClient originalUser;
        TcpClient replacementUser;

        startServer(server);

        connectUser(server, originalUser);

        const uint8_t data[] = { 42 };

        sendRequest(
            originalUser, 1, Priority::HIGH,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.observedRequests == 1;
            }
        );

        const RegisteredRequestInfo original =
            getRequest(server.registry, 1);

        // Stop TUR while the original user is still connected.
        stopServer(server);

        check(
            !getRequest(
                server.registry, 1
            ).userConnected,
            "TUR shutdown did not disconnect the user session"
        );

        checkQueueSizes(
            server.registry, 1, 0, 0
        );

        checkResult(
            originalUser.disconnect(),
            "Original user cleanup failed"
        );

        // Restart the same receiver and keep the registry.
        startServer(server);

        connectUser(server, replacementUser);

        sendRequest(
            replacementUser, 1, Priority::LOW,
            data, sizeof(data)
        );

        waitUntil(
            server,
            [](const TurStatistics& statistics)
            {
                return statistics.activeUsers == 1 &&
                    statistics.observedRequests == 1;
            }
        );

        const RegisteredRequestInfo replacement =
            getRequest(server.registry, 2);

        check(
            original.userSessionId !=
            replacement.userSessionId,
            "TUR restart reused a user session"
        );

        check(
            !getRequest(
                server.registry, 1
            ).userConnected &&
            replacement.userConnected,
            "Old and new user sessions were mixed"
        );

        stopServer(server);

        checkResult(
            replacementUser.disconnect(),
            "Replacement user cleanup failed"
        );

        checkQueueSizes(
            server.registry, 1, 0, 1
        );

        checkSemaphoreCount(
            server.registry, 2
        );

        std::cout
            << "[PASS] TUR request shutdown and restart test\n";
    }
}

// Runs all TUR request integration tests.
void runTurRequestIntegrationTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization failed"
    );

    testInvalidRegistry();

    testNetworkRequestRegistration();

    testMultipleUserSessions();

    testUserReconnect();

    testBufferedRequests();

    testUnregisteredRequest();

    testRegistryCapacity();

    testShutdownAndRestart();

    checkResult(
        context.cleanup(),
        "Winsock cleanup failed"
    );

    std::cout
        << "All TUR request integration tests passed.\n";
}