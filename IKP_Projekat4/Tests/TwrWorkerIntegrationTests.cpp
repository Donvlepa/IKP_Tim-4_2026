#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "TwrReceiver.h"
#include "WorkerRegistry.h"
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
                << " | Error: " << result << "\n";

            std::abort();
        }
    }

    // Owns the worker registry and both receiver threads.
    struct WorkerIntegrationServer
    {
        WorkerRegistry registry;

        ConnectionHandoff handoff;

        TurReceiver tur;

        TwrReceiver twr;

        explicit WorkerIntegrationServer(
            std::size_t capacity =
            WorkerRegistry::MAX_WORKER_RECORDS
        )
            : registry(capacity),
            tur(handoff),
            twr(handoff, registry)
        {
        }

        ~WorkerIntegrationServer()
        {
            tur.stop();

            handoff.seal();

            twr.stop();
        }
    };

    // Starts both receiver threads.
    void startServer(
        WorkerIntegrationServer& server
    )
    {
        check(
            server.registry.isValid(),
            "Worker registry is invalid"
        );

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
            "Invalid server port"
        );
    }

    // Stops the producer before stopping the consumer.
    void stopServer(
        WorkerIntegrationServer& server
    )
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
            "Handoff queue is not empty"
        );

        check(
            server.registry.getActiveWorkerCount() == 0,
            "Registry retained active workers"
        );

        check(
            server.registry.getFreeWorkerCount() == 0,
            "Registry retained free workers"
        );

        check(
            server.twr.getStatistics().activeWorkers == 0,
            "TWR retained worker connections"
        );
    }

    // Waits for a condition while the receiver threads run.
    template<typename Predicate>
    void waitUntil(
        WorkerIntegrationServer& server,
        Predicate predicate
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            const TurStatistics tur =
                server.tur.getStatistics();

            const TwrStatistics twr =
                server.twr.getStatistics();

            checkResult(
                tur.lastError,
                "TUR reported an internal error"
            );

            checkResult(
                twr.lastError,
                "TWR reported an internal error"
            );

            checkResult(
                server.registry.getFatalError(),
                "Worker registry reported an internal error"
            );

            if (predicate(tur, twr))
            {
                return;
            }

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Worker integration test timed out"
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
            "No protocol bytes were sent"
        );
    }

    // Connects and registers a worker.
    void connectWorker(
        WorkerIntegrationServer& server,
        TcpClient& client,
        uint64_t workerId
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                server.tur.getPort()),
            "Worker connection failed"
        );

        RegisterWrPayload registration;

        registration.workerId = workerId;

        sendChecked(client, registration);
    }

    // Sends a heartbeat.
    void sendHeartbeat(TcpClient& client)
    {
        HeartbeatPayload heartbeat;

        sendChecked(client, heartbeat);
    }

    // Retrieves a worker registry record.
    WorkerEntry getWorker(
        WorkerRegistry& registry,
        uint64_t workerId
    )
    {
        WorkerEntry worker;

        check(
            registry.getWorker(
                workerId,
                worker),
            "Expected worker record was not found"
        );

        check(
            worker.workerId == workerId,
            "Incorrect worker ID"
        );

        return worker;
    }

    // Creates a worker session token.
    FreeWorkerToken createToken(
        uint64_t workerId,
        uint64_t sessionId
    )
    {
        FreeWorkerToken token;

        token.workerId = workerId;

        token.sessionId = sessionId;

        return token;
    }

    // Verifies registry and queue counters.
    void checkCounts(
        WorkerRegistry& registry,
        std::size_t records,
        std::size_t active,
        std::size_t free,
        std::size_t queued
    )
    {
        check(
            registry.size() == records,
            "Incorrect worker record count"
        );

        check(
            registry.getActiveWorkerCount() == active,
            "Incorrect active worker count"
        );

        check(
            registry.getFreeWorkerCount() == free,
            "Incorrect free worker count"
        );

        check(
            registry.getFreeQueueSize() == queued,
            "Incorrect physical free queue size"
        );
    }

    // Verifies the actual semaphore count.
    // Call only when no registrations or dispatches are active.
    void checkSemaphoreCount(
        WorkerRegistry& registry,
        LONG expected
    )
    {
        HANDLE semaphore =
            registry.getFreeWorkersSemaphore();

        check(
            semaphore != nullptr,
            "Invalid free worker semaphore"
        );

        for (LONG i = 0; i < expected; ++i)
        {
            check(
                WaitForSingleObject(
                    semaphore, 0
                ) == WAIT_OBJECT_0,
                "Missing free worker semaphore signal"
            );
        }

        check(
            WaitForSingleObject(
                semaphore, 0
            ) == WAIT_TIMEOUT,
            "Unexpected free worker semaphore signal"
        );

        if (expected > 0)
        {
            check(
                ReleaseSemaphore(
                    semaphore,
                    expected,
                    nullptr) != 0,
                "Semaphore restoration failed"
            );
        }
    }

    // Verifies that the server closed a client connection.
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

            if (status == ReceiveStatus::SocketError &&
                socketError == WSAECONNRESET)
            {
                return;
            }

            check(
                status == ReceiveStatus::WouldBlock,
                "Unexpected data while waiting for closure"
            );

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Worker connection closure timed out"
            );

            Sleep(5);
        }
    }

    // Tests startup with an invalid worker registry.
    void testInvalidRegistry()
    {
        WorkerRegistry registry(0);

        ConnectionHandoff handoff;

        TwrReceiver twr(handoff, registry);

        check(
            twr.start() == WSAEINVAL,
            "TWR accepted an invalid worker registry"
        );

        check(
            twr.getStatistics().activeWorkers == 0,
            "Invalid startup created active workers"
        );

        std::cout
            << "[PASS] TWR invalid registry startup test\n";
    }

    // Tests registration through real TCP connections.
    void testNetworkRegistration()
    {
        WorkerIntegrationServer server;

        TcpClient worker1;
        TcpClient worker2;
        TcpClient worker3;

        startServer(server);

        connectWorker(server, worker1, 10);
        connectWorker(server, worker2, 20);
        connectWorker(server, worker3, 30);

        waitUntil(
            server,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.handedOffWorkers == 3 &&
                    twr.activeWorkers == 3 &&
                    twr.registeredWorkers == 3;
            }
        );

        checkCounts(
            server.registry, 3, 3, 3, 3
        );

        const WorkerEntry first =
            getWorker(server.registry, 10);

        const WorkerEntry second =
            getWorker(server.registry, 20);

        const WorkerEntry third =
            getWorker(server.registry, 30);

        const WorkerEntry* workers[] =
        {
            &first,
            &second,
            &third
        };

        for (int i = 0; i < 3; ++i)
        {
            const WorkerEntry& worker =
                *workers[i];

            check(
                worker.status == WorkerStatus::FREE &&
                worker.sessionId != 0 &&
                worker.workerSocket != INVALID_SOCKET &&
                worker.currentRequestId == 0,
                "Incorrect registered worker state"
            );

            check(
                server.registry.isFreeWorker(
                    createToken(
                        worker.workerId,
                        worker.sessionId)),
                "Registered worker is not available"
            );

            TwrWorkerInfo information;

            check(
                server.twr.getWorker(
                    worker.workerId,
                    information),
                "TWR diagnostic worker is missing"
            );

            check(
                information.sessionId ==
                worker.sessionId,
                "TWR and registry sessions differ"
            );

            for (int j = 0; j < i; ++j)
            {
                check(
                    worker.sessionId !=
                    workers[j]->sessionId,
                    "Workers share a session identifier"
                );

                check(
                    worker.workerSocket !=
                    workers[j]->workerSocket,
                    "Workers share a server socket"
                );
            }
        }

        checkSemaphoreCount(
            server.registry, 3
        );

        stopServer(server);

        expectPeerClosed(worker1);
        expectPeerClosed(worker2);
        expectPeerClosed(worker3);

        checkResult(
            worker1.disconnect(),
            "First worker cleanup failed"
        );

        checkResult(
            worker2.disconnect(),
            "Second worker cleanup failed"
        );

        checkResult(
            worker3.disconnect(),
            "Third worker cleanup failed"
        );

        std::cout
            << "       Registered workers: 3\n"
            << "       Free workers: 3\n"
            << "       Semaphore signals: 3\n"
            << "[PASS] TWR network worker registration test\n";
    }

    // Tests heartbeat updates in the real registry.
    void testHeartbeatIntegration()
    {
        WorkerIntegrationServer server;

        TcpClient worker1;
        TcpClient worker2;

        startServer(server);

        connectWorker(server, worker1, 1);
        connectWorker(server, worker2, 2);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 2;
            }
        );

        const WorkerEntry first =
            getWorker(server.registry, 1);

        const WorkerEntry second =
            getWorker(server.registry, 2);

        // Ensure a later monotonic timestamp.
        const ULONGLONG startTime =
            GetTickCount64();

        while (GetTickCount64() <=
            first.lastHeartbeatMs)
        {
            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Heartbeat timestamp did not advance"
            );

            Sleep(1);
        }

        sendHeartbeat(worker1);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.observedHeartbeats == 1;
            }
        );

        const WorkerEntry updated =
            getWorker(server.registry, 1);

        check(
            updated.lastHeartbeatMs >
            first.lastHeartbeatMs,
            "Registry heartbeat was not updated"
        );

        check(
            updated.sessionId == first.sessionId &&
            updated.status == WorkerStatus::FREE,
            "Heartbeat changed the worker assignment state"
        );

        check(
            getWorker(
                server.registry, 2
            ).lastHeartbeatMs ==
            second.lastHeartbeatMs,
            "Unrelated heartbeat timestamp was modified"
        );

        checkCounts(
            server.registry, 2, 2, 2, 2
        );

        checkSemaphoreCount(
            server.registry, 2
        );

        stopServer(server);

        checkResult(
            worker1.disconnect(),
            "First worker cleanup failed"
        );

        checkResult(
            worker2.disconnect(),
            "Second worker cleanup failed"
        );

        std::cout
            << "[PASS] TWR worker registry heartbeat test\n";
    }

    // Tests a duplicate active worker identifier.
    void testDuplicateRegistration()
    {
        WorkerIntegrationServer server;

        TcpClient original;
        TcpClient duplicate;

        startServer(server);

        connectWorker(server, original, 7);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        const WorkerEntry before =
            getWorker(server.registry, 7);

        connectWorker(server, duplicate, 7);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.rejectedWorkers == 1;
            }
        );

        expectPeerClosed(duplicate);

        const WorkerEntry after =
            getWorker(server.registry, 7);

        check(
            after.sessionId == before.sessionId &&
            after.workerSocket == before.workerSocket &&
            after.status == WorkerStatus::FREE,
            "Duplicate registration replaced the original"
        );

        checkCounts(
            server.registry, 1, 1, 1, 1
        );

        checkSemaphoreCount(
            server.registry, 1
        );

        // Verify that the original connection still works.
        sendHeartbeat(original);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.observedHeartbeats == 1;
            }
        );

        stopServer(server);

        checkResult(
            original.disconnect(),
            "Original worker cleanup failed"
        );

        checkResult(
            duplicate.disconnect(),
            "Duplicate worker cleanup failed"
        );

        std::cout
            << "[PASS] TWR duplicate registry registration test\n";
    }

    // Tests DEAD state after a real TCP disconnection.
    void testWorkerDisconnection()
    {
        WorkerIntegrationServer server;

        TcpClient worker;

        startServer(server);

        connectWorker(server, worker, 15);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        const WorkerEntry original =
            getWorker(server.registry, 15);

        const FreeWorkerToken token =
            createToken(
                original.workerId,
                original.sessionId
            );

        checkResult(
            worker.disconnect(),
            "Worker disconnection failed"
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

        const WorkerEntry dead =
            getWorker(server.registry, 15);

        check(
            dead.status == WorkerStatus::DEAD &&
            dead.workerSocket == INVALID_SOCKET &&
            dead.sessionId == original.sessionId,
            "Disconnected worker state is incorrect"
        );

        check(
            !server.registry.isFreeWorker(token),
            "Disconnected worker remains available"
        );

        // The stale token remains in the physical queue.
        checkCounts(
            server.registry, 1, 0, 0, 1
        );

        checkSemaphoreCount(
            server.registry, 1
        );

        stopServer(server);

        std::cout
            << "[PASS] TWR worker disconnection registry test\n";
    }

    // Tests reconnection and stale session protection.
    void testWorkerReconnect()
    {
        WorkerIntegrationServer server;

        TcpClient worker;

        startServer(server);

        connectWorker(server, worker, 42);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        const WorkerEntry original =
            getWorker(server.registry, 42);

        const FreeWorkerToken oldToken =
            createToken(42, original.sessionId);

        checkResult(
            worker.disconnect(),
            "Original worker disconnection failed"
        );

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 0;
            }
        );

        connectWorker(server, worker, 42);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1 &&
                    twr.registeredWorkers == 2;
            }
        );

        const WorkerEntry replacement =
            getWorker(server.registry, 42);

        check(
            replacement.sessionId !=
            original.sessionId,
            "Replacement worker reused an old session"
        );

        check(
            replacement.status == WorkerStatus::FREE,
            "Replacement worker is not free"
        );

        check(
            !server.registry.isFreeWorker(oldToken),
            "Old queue token became valid again"
        );

        check(
            server.registry.isFreeWorker(
                createToken(
                    42,
                    replacement.sessionId)),
            "Replacement queue token is invalid"
        );

        FreeWorkerToken head;

        check(
            server.registry.peekFreeWorker(head) &&
            head.workerId == 42 &&
            head.sessionId == original.sessionId,
            "Expected stale queue head is missing"
        );

        WorkerEntry previous;

        check(
            !server.registry.markDead(
                42,
                original.sessionId,
                previous),
            "Old session killed the replacement worker"
        );

        check(
            !server.registry.updateHeartbeat(
                42,
                original.sessionId,
                GetTickCount64()),
            "Old session heartbeat was accepted"
        );

        sendHeartbeat(worker);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.observedHeartbeats == 1;
            }
        );

        checkCounts(
            server.registry, 1, 1, 1, 2
        );

        checkSemaphoreCount(
            server.registry, 2
        );

        stopServer(server);

        checkResult(
            worker.disconnect(),
            "Replacement worker cleanup failed"
        );

        std::cout
            << "       Old session: "
            << original.sessionId << "\n"
            << "       New session: "
            << replacement.sessionId << "\n"
            << "       Stale queue items: 1\n"
            << "[PASS] TWR worker reconnection registry test\n";
    }

    // Tests registration when the worker registry is full.
    void testRegistryCapacity()
    {
        WorkerIntegrationServer server(1);

        TcpClient original;
        TcpClient extra;

        startServer(server);

        connectWorker(server, original, 1);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        connectWorker(server, extra, 2);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.rejectedWorkers == 1;
            }
        );

        expectPeerClosed(extra);

        WorkerEntry missing;

        check(
            !server.registry.getWorker(2, missing),
            "Rejected worker exists in the registry"
        );

        check(
            server.registry.isValid(),
            "Registry capacity rejection corrupted state"
        );

        checkCounts(
            server.registry, 1, 1, 1, 1
        );

        checkSemaphoreCount(
            server.registry, 1
        );

        stopServer(server);

        checkResult(
            original.disconnect(),
            "Original worker cleanup failed"
        );

        checkResult(
            extra.disconnect(),
            "Rejected worker cleanup failed"
        );

        std::cout
            << "[PASS] TWR worker registry capacity test\n";
    }

    // Tests invalid messages from registered workers.
    void testInvalidWorkerMessage()
    {
        WorkerIntegrationServer server;

        TcpClient worker;

        startServer(server);

        connectWorker(server, worker, 11);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 1;
            }
        );

        // Registered workers cannot register again.
        RegisterWrPayload registration;

        registration.workerId = 99;

        sendChecked(worker, registration);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 0 &&
                    twr.rejectedWorkers == 1;
            }
        );

        expectPeerClosed(worker);

        check(
            getWorker(
                server.registry, 11
            ).status == WorkerStatus::DEAD,
            "Rejected worker was not marked DEAD"
        );

        WorkerEntry missing;

        check(
            !server.registry.getWorker(99, missing),
            "Invalid message created another worker"
        );

        checkCounts(
            server.registry, 1, 0, 0, 1
        );

        checkSemaphoreCount(
            server.registry, 1
        );

        stopServer(server);

        checkResult(
            worker.disconnect(),
            "Rejected worker cleanup failed"
        );

        std::cout
            << "[PASS] TWR invalid worker message registry test\n";
    }

    // Tests shutdown while workers remain connected.
    void testShutdown()
    {
        WorkerIntegrationServer server;

        TcpClient worker1;
        TcpClient worker2;

        startServer(server);

        connectWorker(server, worker1, 100);
        connectWorker(server, worker2, 200);

        waitUntil(
            server,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.activeWorkers == 2;
            }
        );

        stopServer(server);

        expectPeerClosed(worker1);
        expectPeerClosed(worker2);

        const WorkerEntry first =
            getWorker(server.registry, 100);

        const WorkerEntry second =
            getWorker(server.registry, 200);

        check(
            first.status == WorkerStatus::DEAD &&
            second.status == WorkerStatus::DEAD &&
            first.workerSocket == INVALID_SOCKET &&
            second.workerSocket == INVALID_SOCKET,
            "Shutdown did not release worker sessions"
        );

        checkCounts(
            server.registry, 2, 0, 0, 2
        );

        checkSemaphoreCount(
            server.registry, 2
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
            << "[PASS] TWR worker registry shutdown test\n";
    }
}

// Runs all TWR worker registry integration tests.
void runTwrWorkerIntegrationTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization failed"
    );

    testInvalidRegistry();

    testNetworkRegistration();

    testHeartbeatIntegration();

    testDuplicateRegistration();

    testWorkerDisconnection();

    testWorkerReconnect();

    testRegistryCapacity();

    testInvalidWorkerMessage();

    testShutdown();

    checkResult(
        context.cleanup(),
        "Winsock cleanup failed"
    );

    std::cout
        << "All TWR worker integration tests passed.\n";
}