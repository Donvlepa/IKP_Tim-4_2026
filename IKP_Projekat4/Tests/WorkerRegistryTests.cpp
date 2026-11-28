#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstddef>

#include "WorkerRegistry.h"
#include "TestSuites.h"

namespace
{
    // Synthetic socket values used only as registry data.
    constexpr SOCKET WORKER_SOCKET_1 =
        static_cast<SOCKET>(100);

    constexpr SOCKET WORKER_SOCKET_2 =
        static_cast<SOCKET>(200);

    constexpr SOCKET WORKER_SOCKET_3 =
        static_cast<SOCKET>(300);

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

    // Registers a worker and returns its generated session ID.
    uint64_t registerChecked(
        WorkerRegistry& registry,
        uint64_t workerId,
        SOCKET socketHandle,
        uint64_t heartbeatTimeMs = 1000
    )
    {
        uint64_t sessionId = 0;

        checkResult(
            registry.registerWorker(
                workerId,
                socketHandle,
                heartbeatTimeMs,
                sessionId),
            "Worker registration failed"
        );

        check(
            sessionId != 0,
            "Generated worker session is invalid"
        );

        return sessionId;
    }

    // Returns a synchronized worker record.
    WorkerEntry getWorker(
        WorkerRegistry& registry,
        uint64_t workerId
    )
    {
        WorkerEntry information;

        check(
            registry.getWorker(
                workerId,
                information),
            "Registered worker was not found"
        );

        check(
            information.workerId == workerId,
            "Incorrect worker identifier"
        );

        return information;
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

    // Verifies all registry counters.
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
            "Incorrect free worker queue size"
        );
    }

    // Checks and restores the semaphore count.
    // No producer or consumer threads may be active.
    void checkSemaphoreCount(
        WorkerRegistry& registry,
        LONG expectedCount
    )
    {
        HANDLE semaphore =
            registry.getFreeWorkersSemaphore();

        check(
            semaphore != nullptr,
            "Free worker semaphore is invalid"
        );

        for (LONG i = 0;
            i < expectedCount;
            ++i)
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

        if (expectedCount > 0)
        {
            check(
                ReleaseSemaphore(
                    semaphore,
                    expectedCount,
                    nullptr
                ) != 0,
                "Semaphore signal restoration failed"
            );
        }
    }

    // Tests initial state and invalid capacities.
    void testInitialState()
    {
        WorkerRegistry registry;

        check(
            registry.isValid(),
            "Worker registry initialization failed"
        );

        checkCounts(registry, 0, 0, 0, 0);

        checkSemaphoreCount(registry, 0);

        WorkerEntry information;

        check(
            !registry.getWorker(1, information),
            "Empty registry returned a worker"
        );

        FreeWorkerToken token;

        check(
            !registry.peekFreeWorker(token),
            "Empty queue returned a worker token"
        );

        check(
            !registry.isFreeWorker(token),
            "Empty worker token was accepted"
        );

        WorkerRegistry invalidWorkers(0);

        check(
            !invalidWorkers.isValid(),
            "Zero worker capacity was accepted"
        );

        WorkerRegistry invalidQueue(10, 0);

        check(
            !invalidQueue.isValid(),
            "Zero queue capacity was accepted"
        );

        WorkerRegistry invalidLarge(
            WorkerRegistry::MAX_WORKER_RECORDS + 1
        );

        check(
            !invalidLarge.isValid(),
            "Invalid registry capacity was accepted"
        );

        WorkerRegistry invalidLargeQueue(
            10,
            WorkerRegistry::MAX_QUEUE_ITEMS + 1
        );

        check(
            !invalidLargeQueue.isValid(),
            "Invalid queue capacity was accepted"
        );

        std::cout
            << "[PASS] Initial worker registry state test\n";
    }

    // Tests registration and worker metadata.
    void testWorkerRegistration()
    {
        WorkerRegistry registry;

        const uint64_t sessionId =
            registerChecked(
                registry,
                42,
                WORKER_SOCKET_1,
                1500
            );

        check(
            sessionId == 1,
            "Incorrect first worker session"
        );

        const WorkerEntry worker =
            getWorker(registry, 42);

        check(
            worker.sessionId == sessionId,
            "Worker session was not stored"
        );

        check(
            worker.workerSocket == WORKER_SOCKET_1,
            "Incorrect worker socket"
        );

        check(
            worker.status == WorkerStatus::FREE,
            "New worker is not free"
        );

        check(
            worker.lastHeartbeatMs == 1500,
            "Incorrect initial heartbeat timestamp"
        );

        check(
            worker.currentRequestId == 0,
            "New worker already has a request"
        );

        FreeWorkerToken token;

        check(
            registry.peekFreeWorker(token),
            "Registered worker is missing from the queue"
        );

        check(
            token.workerId == 42 &&
            token.sessionId == sessionId,
            "Incorrect queued worker session"
        );

        check(
            registry.isFreeWorker(token),
            "Registered free worker token is invalid"
        );

        checkCounts(registry, 1, 1, 1, 1);

        checkSemaphoreCount(registry, 1);

        std::cout
            << "[PASS] Worker registration metadata test\n";
    }

    // Tests multiple registrations and the queue head.
    void testMultipleWorkers()
    {
        WorkerRegistry registry;

        const uint64_t first =
            registerChecked(
                registry, 10, WORKER_SOCKET_1);

        const uint64_t second =
            registerChecked(
                registry, 20, WORKER_SOCKET_2);

        const uint64_t third =
            registerChecked(
                registry, 30, WORKER_SOCKET_3);

        check(
            first == 1 &&
            second == 2 &&
            third == 3,
            "Worker sessions are not sequential"
        );

        checkCounts(registry, 3, 3, 3, 3);

        FreeWorkerToken token;

        check(
            registry.peekFreeWorker(token) &&
            token.workerId == 10 &&
            token.sessionId == first,
            "Incorrect first free worker token"
        );

        check(
            registry.isFreeWorker(
                createToken(20, second)),
            "Second free worker is missing"
        );

        check(
            registry.isFreeWorker(
                createToken(30, third)),
            "Third free worker is missing"
        );

        checkSemaphoreCount(registry, 3);

        std::cout
            << "       Active workers: 3\n"
            << "       Free queue items: 3\n"
            << "       Semaphore signals: 3\n"
            << "[PASS] Multiple worker registration test\n";
    }

    // Tests duplicate active worker rejection.
    void testDuplicateWorker()
    {
        WorkerRegistry registry;

        const uint64_t originalSession =
            registerChecked(
                registry, 7, WORKER_SOCKET_1);

        uint64_t duplicateSession = 999;

        check(
            registry.registerWorker(
                7,
                WORKER_SOCKET_2,
                2000,
                duplicateSession
            ) == WSAEALREADY,
            "Duplicate active worker was accepted"
        );

        check(
            duplicateSession == 0,
            "Rejected worker received a session ID"
        );

        const WorkerEntry original =
            getWorker(registry, 7);

        check(
            original.sessionId == originalSession &&
            original.workerSocket == WORKER_SOCKET_1 &&
            original.lastHeartbeatMs == 1000,
            "Duplicate registration changed the worker"
        );

        checkCounts(registry, 1, 1, 1, 1);

        checkSemaphoreCount(registry, 1);

        check(
            registerChecked(
                registry, 8, WORKER_SOCKET_2
            ) == 2,
            "Rejected registration consumed a session ID"
        );

        checkCounts(registry, 2, 2, 2, 2);

        checkSemaphoreCount(registry, 2);

        std::cout
            << "[PASS] Duplicate worker rejection test\n";
    }

    // Tests valid, invalid and stale heartbeat updates.
    void testHeartbeatUpdates()
    {
        WorkerRegistry registry;

        const uint64_t firstSession =
            registerChecked(
                registry, 1, WORKER_SOCKET_1);

        const uint64_t secondSession =
            registerChecked(
                registry, 2, WORKER_SOCKET_2);

        check(
            registry.updateHeartbeat(
                1, firstSession, 2000),
            "Valid heartbeat was rejected"
        );

        check(
            getWorker(
                registry, 1
            ).lastHeartbeatMs == 2000,
            "Heartbeat timestamp was not updated"
        );

        check(
            getWorker(
                registry, 2
            ).lastHeartbeatMs == 1000,
            "Unrelated worker heartbeat was modified"
        );

        check(
            !registry.updateHeartbeat(
                1, secondSession, 3000),
            "Heartbeat from a wrong session was accepted"
        );

        check(
            !registry.updateHeartbeat(
                999, firstSession, 3000),
            "Unknown worker heartbeat was accepted"
        );

        check(
            !registry.updateHeartbeat(
                1, 0, 3000),
            "Invalid session heartbeat was accepted"
        );

        // A valid older timestamp must not move time backwards.
        check(
            registry.updateHeartbeat(
                1, firstSession, 1500),
            "Valid session heartbeat was rejected"
        );

        check(
            getWorker(
                registry, 1
            ).lastHeartbeatMs == 2000,
            "Heartbeat timestamp moved backwards"
        );

        checkCounts(registry, 2, 2, 2, 2);

        checkSemaphoreCount(registry, 2);

        std::cout
            << "[PASS] Worker heartbeat update test\n";
    }

    // Tests DEAD state and stale queue token preservation.
    void testWorkerFailure()
    {
        WorkerRegistry registry;

        const uint64_t sessionId =
            registerChecked(
                registry, 15, WORKER_SOCKET_1);

        const FreeWorkerToken token =
            createToken(15, sessionId);

        WorkerEntry previous;

        check(
            registry.markDead(
                15, sessionId, previous),
            "Worker failure was not recorded"
        );

        check(
            previous.status == WorkerStatus::FREE &&
            previous.sessionId == sessionId &&
            previous.workerSocket == WORKER_SOCKET_1,
            "Previous worker state was not preserved"
        );

        const WorkerEntry dead =
            getWorker(registry, 15);

        check(
            dead.status == WorkerStatus::DEAD,
            "Worker was not marked DEAD"
        );

        check(
            dead.workerSocket == INVALID_SOCKET,
            "DEAD worker retained an active socket value"
        );

        check(
            dead.sessionId == sessionId &&
            dead.currentRequestId == 0,
            "Incorrect DEAD worker metadata"
        );

        check(
            !registry.isFreeWorker(token),
            "DEAD worker token remains valid"
        );

        check(
            !registry.updateHeartbeat(
                15, sessionId, 5000),
            "DEAD worker heartbeat was accepted"
        );

        check(
            !registry.markDead(
                15, sessionId, previous),
            "Worker failure was applied twice"
        );

        // The physical queue token remains present.
        checkCounts(registry, 1, 0, 0, 1);

        checkSemaphoreCount(registry, 1);

        FreeWorkerToken stale;

        check(
            registry.peekFreeWorker(stale) &&
            stale.workerId == 15 &&
            stale.sessionId == sessionId,
            "Stale queue token was removed prematurely"
        );

        std::cout
            << "[PASS] Worker DEAD state and stale token test\n";
    }

    // Tests reconnection and protection against old sessions.
    void testWorkerReconnect()
    {
        WorkerRegistry registry;

        const uint64_t oldSession =
            registerChecked(
                registry, 7, WORKER_SOCKET_1);

        WorkerEntry previous;

        check(
            registry.markDead(
                7, oldSession, previous),
            "Original worker failure was not recorded"
        );

        const uint64_t newSession =
            registerChecked(
                registry,
                7,
                WORKER_SOCKET_2,
                5000
            );

        check(
            newSession != oldSession,
            "Worker reconnection reused a session ID"
        );

        const WorkerEntry replacement =
            getWorker(registry, 7);

        check(
            replacement.sessionId == newSession &&
            replacement.status == WorkerStatus::FREE &&
            replacement.workerSocket == WORKER_SOCKET_2 &&
            replacement.lastHeartbeatMs == 5000,
            "Replacement worker state is incorrect"
        );

        check(
            !registry.isFreeWorker(
                createToken(7, oldSession)),
            "Old worker token became valid again"
        );

        check(
            registry.isFreeWorker(
                createToken(7, newSession)),
            "New worker token is invalid"
        );

        check(
            !registry.updateHeartbeat(
                7, oldSession, 6000),
            "Stale session heartbeat was accepted"
        );

        check(
            !registry.markDead(
                7, oldSession, previous),
            "Stale disconnection killed the new session"
        );

        check(
            getWorker(
                registry, 7
            ).status == WorkerStatus::FREE,
            "Replacement worker was incorrectly disconnected"
        );

        check(
            registry.updateHeartbeat(
                7, newSession, 7000),
            "Replacement heartbeat was rejected"
        );

        check(
            getWorker(
                registry, 7
            ).lastHeartbeatMs == 7000,
            "Replacement heartbeat was not recorded"
        );

        // One record, one active worker, two physical tokens.
        checkCounts(registry, 1, 1, 1, 2);

        checkSemaphoreCount(registry, 2);

        std::cout
            << "       Old session: " << oldSession << "\n"
            << "       New session: " << newSession << "\n"
            << "[PASS] Worker reconnection and session protection test\n";
    }

    // Tests invalid registration parameters.
    void testInvalidRegistration()
    {
        WorkerRegistry registry;

        uint64_t sessionId = 999;

        check(
            registry.registerWorker(
                0,
                WORKER_SOCKET_1,
                1000,
                sessionId
            ) == WSAEINVAL &&
            sessionId == 0,
            "Invalid worker ID was accepted"
        );

        check(
            registry.registerWorker(
                1,
                INVALID_SOCKET,
                1000,
                sessionId
            ) == WSAEINVAL,
            "Invalid worker socket was accepted"
        );

        WorkerEntry previous;

        check(
            !registry.markDead(1, 1, previous),
            "Unknown worker was marked DEAD"
        );

        checkCounts(registry, 0, 0, 0, 0);

        checkSemaphoreCount(registry, 0);

        check(
            registerChecked(
                registry, 1, WORKER_SOCKET_1
            ) == 1,
            "Invalid registrations consumed session IDs"
        );

        checkCounts(registry, 1, 1, 1, 1);

        checkSemaphoreCount(registry, 1);

        std::cout
            << "[PASS] Invalid worker registration test\n";
    }

    // Tests registry capacity and reconnection at capacity.
    void testRegistryCapacity()
    {
        WorkerRegistry registry(2, 10);

        const uint64_t firstSession =
            registerChecked(
                registry, 1, WORKER_SOCKET_1);

        registerChecked(
            registry, 2, WORKER_SOCKET_2);

        uint64_t sessionId = 0;

        check(
            registry.registerWorker(
                3,
                WORKER_SOCKET_3,
                1000,
                sessionId
            ) == WSAENOBUFS,
            "Full registry accepted a new worker ID"
        );

        check(
            sessionId == 0,
            "Rejected worker received a session ID"
        );

        WorkerEntry previous;

        check(
            registry.markDead(
                1, firstSession, previous),
            "Worker failure was not recorded"
        );

        // Reuse the existing record even at full capacity.
        const uint64_t replacementSession =
            registerChecked(
                registry, 1, WORKER_SOCKET_3);

        check(
            replacementSession == 3,
            "Reconnection at capacity failed"
        );

        checkCounts(registry, 2, 2, 2, 3);

        checkSemaphoreCount(registry, 3);

        check(
            registry.isValid(),
            "Registry capacity rejection corrupted state"
        );

        std::cout
            << "[PASS] Worker registry capacity test\n";
    }

    // Tests bounded physical queue capacity.
    void testQueueCapacity()
    {
        WorkerRegistry registry(10, 2);

        const uint64_t firstSession =
            registerChecked(
                registry, 1, WORKER_SOCKET_1);

        registerChecked(
            registry, 2, WORKER_SOCKET_2);

        WorkerEntry previous;

        check(
            registry.markDead(
                1, firstSession, previous),
            "Worker failure was not recorded"
        );

        uint64_t sessionId = 0;

        // Both physical queue slots remain occupied.
        check(
            registry.registerWorker(
                1,
                WORKER_SOCKET_3,
                2000,
                sessionId
            ) == WSAENOBUFS,
            "Full queue accepted another session"
        );

        check(
            sessionId == 0,
            "Rejected replacement received a session ID"
        );

        const WorkerEntry dead =
            getWorker(registry, 1);

        check(
            dead.status == WorkerStatus::DEAD &&
            dead.sessionId == firstSession,
            "Failed registration modified a DEAD worker"
        );

        checkCounts(registry, 2, 1, 1, 2);

        checkSemaphoreCount(registry, 2);

        check(
            registry.isValid(),
            "Queue capacity rejection corrupted the registry"
        );

        std::cout
            << "[PASS] Free worker queue capacity test\n";
    }

    constexpr std::size_t WORKERS_PER_THREAD = 100;

    // Contains parameters for a concurrent worker producer.
    struct ProducerTask
    {
        WorkerRegistry* registry = nullptr;

        uint64_t firstWorkerId = 0;

        uint64_t sessions[WORKERS_PER_THREAD] = {};

        int result = 0;
    };

    // Registers workers in a separate Windows thread.
    unsigned __stdcall producerThread(void* parameter)
    {
        ProducerTask* task =
            static_cast<ProducerTask*>(parameter);

        for (std::size_t i = 0;
            i < WORKERS_PER_THREAD;
            ++i)
        {
            const uint64_t workerId =
                task->firstWorkerId + i;

            const SOCKET syntheticSocket =
                static_cast<SOCKET>(1000 + workerId);

            task->result =
                task->registry->registerWorker(
                    workerId,
                    syntheticSocket,
                    1000 + workerId,
                    task->sessions[i]
                );

            if (task->result != 0)
            {
                return 1;
            }
        }

        return 0;
    }

    // Starts a producer thread.
    HANDLE startProducer(ProducerTask& task)
    {
        const uintptr_t handle =
            _beginthreadex(
                nullptr,
                0,
                producerThread,
                &task,
                0,
                nullptr
            );

        check(
            handle != 0,
            "Worker producer thread creation failed"
        );

        return reinterpret_cast<HANDLE>(handle);
    }

    // Waits for a producer and releases its thread handle.
    void joinProducer(HANDLE thread)
    {
        check(
            WaitForSingleObject(
                thread, 10000
            ) == WAIT_OBJECT_0,
            "Worker producer did not finish"
        );

        check(
            CloseHandle(thread) != 0,
            "Worker producer handle cleanup failed"
        );
    }

    // Tests concurrent worker registration.
    void testConcurrentRegistration()
    {
        WorkerRegistry registry;

        ProducerTask first;

        first.registry = &registry;
        first.firstWorkerId = 1;

        ProducerTask second;

        second.registry = &registry;

        second.firstWorkerId =
            WORKERS_PER_THREAD + 1;

        HANDLE firstThread =
            startProducer(first);

        HANDLE secondThread =
            startProducer(second);

        joinProducer(firstThread);
        joinProducer(secondThread);

        checkResult(
            first.result,
            "First worker producer failed"
        );

        checkResult(
            second.result,
            "Second worker producer failed"
        );

        constexpr std::size_t TOTAL_WORKERS =
            WORKERS_PER_THREAD * 2;

        bool seen[TOTAL_WORKERS + 1] = {};

        ProducerTask* tasks[] =
        {
            &first,
            &second
        };

        // Verify every generated session and worker record.
        for (int t = 0; t < 2; ++t)
        {
            ProducerTask& task = *tasks[t];

            for (std::size_t i = 0;
                i < WORKERS_PER_THREAD;
                ++i)
            {
                const uint64_t workerId =
                    task.firstWorkerId + i;

                const uint64_t sessionId =
                    task.sessions[i];

                check(
                    sessionId > 0 &&
                    sessionId <= TOTAL_WORKERS,
                    "Concurrent session ID is invalid"
                );

                check(
                    !seen[sessionId],
                    "Duplicate concurrent session ID"
                );

                seen[sessionId] = true;

                const WorkerEntry worker =
                    getWorker(registry, workerId);

                check(
                    worker.sessionId == sessionId &&
                    worker.status == WorkerStatus::FREE &&
                    worker.currentRequestId == 0 &&
                    worker.lastHeartbeatMs == 1000 + workerId &&
                    worker.workerSocket ==
                    static_cast<SOCKET>(1000 + workerId),
                    "Concurrent worker metadata was corrupted"
                );

                check(
                    registry.isFreeWorker(
                        createToken(workerId, sessionId)),
                    "Concurrent free worker token is invalid"
                );
            }
        }

        for (std::size_t i = 1;
            i <= TOTAL_WORKERS;
            ++i)
        {
            check(
                seen[i],
                "Concurrent session identifier is missing"
            );
        }

        checkCounts(
            registry,
            TOTAL_WORKERS,
            TOTAL_WORKERS,
            TOTAL_WORKERS,
            TOTAL_WORKERS
        );

        checkSemaphoreCount(
            registry,
            static_cast<LONG>(TOTAL_WORKERS)
        );

        check(
            registry.getFatalError() == 0,
            "Worker registry reported an internal failure"
        );

        std::cout
            << "       Registered workers: "
            << TOTAL_WORKERS << "\n"
            << "       Unique sessions: "
            << TOTAL_WORKERS << "\n"
            << "       Semaphore signals: "
            << TOTAL_WORKERS << "\n"
            << "[PASS] Concurrent worker registration test\n";
    }
}

// Runs all worker registry tests.
void runWorkerRegistryTests()
{
    testInitialState();
    testWorkerRegistration();
    testMultipleWorkers();
    testDuplicateWorker();
    testHeartbeatUpdates();
    testWorkerFailure();
    testWorkerReconnect();
    testInvalidRegistration();
    testRegistryCapacity();
    testQueueCapacity();
    testConcurrentRegistration();

    std::cout
        << "All worker registry tests passed.\n";
}