#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "TwrReceiver.h"
#include "RequestRegistry.h"
#include "WorkerRegistry.h"
#include "MessageIO.h"
#include "TestSuites.h"

namespace
{
    constexpr const char* LOCAL_ADDRESS = "127.0.0.1";

    constexpr uint32_t TEST_TIMEOUT_MS = 10000;

    constexpr std::size_t USER_COUNT = 3;
    constexpr std::size_t WORKER_COUNT = 3;

    constexpr uint64_t REQUESTS_PER_USER = 200;
    constexpr uint64_t HEARTBEATS_PER_WORKER = 100;

    constexpr uint64_t TOTAL_REQUESTS =
        USER_COUNT * REQUESTS_PER_USER;

    constexpr uint64_t TOTAL_HEARTBEATS =
        WORKER_COUNT * HEARTBEATS_PER_WORKER;

    constexpr Priority PRIORITIES[] =
    {
        Priority::HIGH,
        Priority::MEDIUM,
        Priority::LOW
    };

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

    // Owns all shared receiver resources.
    struct ReceiverSystem
    {
        RequestRegistry requests;
        WorkerRegistry workers;

        ConnectionHandoff handoff;

        TurReceiver tur;
        TwrReceiver twr;

        ReceiverSystem()
            : tur(handoff, requests),
            twr(handoff, workers)
        {
        }

        ~ReceiverSystem()
        {
            tur.stop();
            handoff.seal();
            twr.stop();
        }
    };

    // Starts both receivers.
    void startSystem(ReceiverSystem& system)
    {
        check(
            system.requests.isValid(),
            "Request Registry is invalid"
        );

        check(
            system.workers.isValid(),
            "Worker Registry is invalid"
        );

        checkResult(
            system.twr.start(),
            "TWR startup failed"
        );

        checkResult(
            system.tur.start(LOCAL_ADDRESS, 0),
            "TUR startup failed"
        );

        check(
            system.tur.getPort() != 0,
            "Invalid listening port"
        );
    }

    // Stops both receivers in ownership order.
    void stopSystem(ReceiverSystem& system)
    {
        checkResult(
            system.tur.stop(),
            "TUR shutdown failed"
        );

        system.handoff.seal();

        checkResult(
            system.twr.stop(),
            "TWR shutdown failed"
        );

        check(
            system.handoff.isEmpty(),
            "Handoff queue retained connections"
        );

        const TurStatistics tur =
            system.tur.getStatistics();

        const TwrStatistics twr =
            system.twr.getStatistics();

        check(
            tur.activeUsers == 0 &&
            tur.pendingConnections == 0,
            "TUR retained active connections"
        );

        check(
            twr.activeWorkers == 0,
            "TWR retained active workers"
        );

        check(
            system.workers.getActiveWorkerCount() == 0 &&
            system.workers.getFreeWorkerCount() == 0,
            "Worker Registry retained active workers"
        );
    }

    // Waits for a synchronized system condition.
    template<typename Predicate>
    void waitUntil(
        ReceiverSystem& system,
        Predicate predicate
    )
    {
        const ULONGLONG startTime =
            GetTickCount64();

        while (true)
        {
            const TurStatistics tur =
                system.tur.getStatistics();

            const TwrStatistics twr =
                system.twr.getStatistics();

            checkResult(
                tur.lastError,
                "TUR reported an internal error"
            );

            checkResult(
                twr.lastError,
                "TWR reported an internal error"
            );

            checkResult(
                system.requests.getFatalError(),
                "Request Registry reported an error"
            );

            checkResult(
                system.workers.getFatalError(),
                "Worker Registry reported an error"
            );

            if (predicate(tur, twr))
            {
                return;
            }

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "System integration condition timed out"
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

    // Connects and registers a user.
    void connectUser(
        ReceiverSystem& system,
        TcpClient& client
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                system.tur.getPort()),
            "User connection failed"
        );

        RegisterUrPayload registration;

        sendChecked(client, registration);
    }

    // Connects and registers a worker.
    void connectWorker(
        ReceiverSystem& system,
        TcpClient& client,
        uint64_t workerId
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                system.tur.getPort()),
            "Worker connection failed"
        );

        RegisterWrPayload registration;

        registration.workerId = workerId;

        sendChecked(client, registration);
    }

    // Creates a deterministic binary payload.
    void buildRequestData(
        std::size_t userIndex,
        uint64_t requestId,
        uint8_t* data
    )
    {
        data[0] = static_cast<uint8_t>(userIndex + 1);
        data[1] = static_cast<uint8_t>(requestId & 0xFF);
        data[2] = static_cast<uint8_t>((requestId >> 8) & 0xFF);
        data[3] = 0;
    }

    // Sends a user request with a deterministic payload.
    void sendRequest(
        TcpClient& client,
        std::size_t userIndex,
        uint64_t requestId
    )
    {
        uint8_t data[4] = {};

        buildRequestData(
            userIndex,
            requestId,
            data
        );

        RequestPayload request;

        request.clientRequestId = requestId;
        request.priority = PRIORITIES[userIndex];

        request.data = data;
        request.dataSize =
            static_cast<uint32_t>(sizeof(data));

        sendChecked(client, request);
    }

    // Contains parameters for a concurrent sender.
    struct SenderTask
    {
        TcpClient* client = nullptr;

        HANDLE startEvent = nullptr;

        std::size_t index = 0;

        bool worker = false;

        uint64_t sentCount = 0;
    };

    // Sends user requests or worker heartbeats.
    unsigned __stdcall senderThread(void* parameter)
    {
        SenderTask* task =
            static_cast<SenderTask*>(parameter);

        check(
            WaitForSingleObject(
                task->startEvent,
                TEST_TIMEOUT_MS
            ) == WAIT_OBJECT_0,
            "Sender start event failed"
        );

        if (task->worker)
        {
            HeartbeatPayload heartbeat;

            for (uint64_t i = 0;
                i < HEARTBEATS_PER_WORKER;
                ++i)
            {
                sendChecked(
                    *task->client,
                    heartbeat
                );

                ++task->sentCount;
            }
        }
        else
        {
            for (uint64_t i = 1;
                i <= REQUESTS_PER_USER;
                ++i)
            {
                sendRequest(
                    *task->client,
                    task->index,
                    i
                );

                ++task->sentCount;
            }
        }

        return 0;
    }

    // Starts a concurrent sender.
    HANDLE startSender(SenderTask& task)
    {
        const uintptr_t handle =
            _beginthreadex(
                nullptr,
                0,
                senderThread,
                &task,
                0,
                nullptr
            );

        check(
            handle != 0,
            "Sender thread creation failed"
        );

        return reinterpret_cast<HANDLE>(handle);
    }

    // Runs six concurrent TCP sender threads.
    void sendConcurrentMessages(
        TcpClient* users,
        TcpClient* workers
    )
    {
        constexpr std::size_t THREAD_COUNT =
            USER_COUNT + WORKER_COUNT;

        SenderTask tasks[THREAD_COUNT] = {};

        HANDLE threads[THREAD_COUNT] = {};

        HANDLE startEvent =
            CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                nullptr
            );

        check(
            startEvent != nullptr,
            "Sender start event creation failed"
        );

        for (std::size_t i = 0;
            i < USER_COUNT;
            ++i)
        {
            tasks[i].client = &users[i];
            tasks[i].startEvent = startEvent;
            tasks[i].index = i;
            tasks[i].worker = false;

            threads[i] =
                startSender(tasks[i]);
        }

        for (std::size_t i = 0;
            i < WORKER_COUNT;
            ++i)
        {
            const std::size_t index =
                USER_COUNT + i;

            tasks[index].client = &workers[i];
            tasks[index].startEvent = startEvent;
            tasks[index].index = i;
            tasks[index].worker = true;

            threads[index] =
                startSender(tasks[index]);
        }

        // Release all sender threads together.
        check(
            SetEvent(startEvent) != 0,
            "Sender start event signaling failed"
        );

        check(
            WaitForMultipleObjects(
                static_cast<DWORD>(THREAD_COUNT),
                threads,
                TRUE,
                30000
            ) == WAIT_OBJECT_0,
            "Concurrent senders did not finish"
        );

        for (std::size_t i = 0;
            i < THREAD_COUNT;
            ++i)
        {
            const uint64_t expected =
                tasks[i].worker
                ? HEARTBEATS_PER_WORKER
                : REQUESTS_PER_USER;

            check(
                tasks[i].sentCount == expected,
                "Incorrect sender message count"
            );

            check(
                CloseHandle(threads[i]) != 0,
                "Sender thread handle cleanup failed"
            );
        }

        check(
            CloseHandle(startEvent) != 0,
            "Sender start event cleanup failed"
        );
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
            "Expected request was not found"
        );

        check(
            information.lbRequestId == lbRequestId,
            "Incorrect internal request ID"
        );

        return information;
    }

    // Retrieves registered worker information.
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
            "Expected worker was not found"
        );

        check(
            information.workerId == workerId,
            "Incorrect worker ID"
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

    // Verifies and restores a semaphore count.
    // No semaphore producers or consumers may be active.
    void checkSemaphoreCount(
        HANDLE semaphore,
        LONG expectedCount
    )
    {
        check(
            semaphore != nullptr,
            "Invalid semaphore handle"
        );

        for (LONG i = 0;
            i < expectedCount;
            ++i)
        {
            check(
                WaitForSingleObject(
                    semaphore, 0
                ) == WAIT_OBJECT_0,
                "Missing semaphore signal"
            );
        }

        check(
            WaitForSingleObject(
                semaphore, 0
            ) == WAIT_TIMEOUT,
            "Unexpected semaphore signal"
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

    // Verifies closure of a TCP connection.
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

            if (status == ReceiveStatus::ConnectionClosed)
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
                "Unexpected message or socket error"
            );

            check(
                GetTickCount64() - startTime <
                TEST_TIMEOUT_MS,
                "Peer connection closure timed out"
            );

            Sleep(5);
        }
    }

    // Verifies every request received from concurrent users.
    void verifyRequests(
        ReceiverSystem& system,
        uint64_t* userSessions
    )
    {
        uint64_t expectedIds[USER_COUNT] =
        {
            1, 1, 1
        };

        uint64_t firstLbIds[USER_COUNT] = {};

        SOCKET userSockets[USER_COUNT] =
        {
            INVALID_SOCKET,
            INVALID_SOCKET,
            INVALID_SOCKET
        };

        check(
            system.requests.size() == TOTAL_REQUESTS,
            "Incorrect total registered request count"
        );

        for (uint64_t lbId = 1;
            lbId <= TOTAL_REQUESTS;
            ++lbId)
        {
            const RegisteredRequestInfo information =
                getRequest(system.requests, lbId);

            uint8_t data[4] = {};

            uint32_t copied = 0;

            check(
                system.requests.copyRequestData(
                    lbId,
                    data,
                    sizeof(data),
                    copied),
                "Stored request data copy failed"
            );

            check(
                copied == sizeof(data),
                "Incorrect stored payload size"
            );

            check(
                data[0] >= 1 &&
                data[0] <= USER_COUNT,
                "Invalid payload user index"
            );

            const std::size_t userIndex =
                static_cast<std::size_t>(data[0] - 1);

            const uint64_t expectedClientId =
                expectedIds[userIndex];

            check(
                expectedClientId <= REQUESTS_PER_USER,
                "Too many requests from one user"
            );

            check(
                information.clientRequestId ==
                expectedClientId,
                "User request order was violated"
            );

            check(
                information.priority ==
                PRIORITIES[userIndex],
                "Request priority was corrupted"
            );

            check(
                information.status == RequestStatus::QUEUED &&
                information.attemptId == 0 &&
                information.assignedWorkerId == 0 &&
                information.userConnected &&
                !information.responseSent,
                "Incorrect queued request state"
            );

            check(
                information.userSessionId != 0 &&
                information.userSocket != INVALID_SOCKET &&
                information.dataSize == sizeof(data),
                "Invalid user session association"
            );

            // Remember the first request of each user.
            if (firstLbIds[userIndex] == 0)
            {
                firstLbIds[userIndex] = lbId;

                userSessions[userIndex] =
                    information.userSessionId;

                userSockets[userIndex] =
                    information.userSocket;
            }

            check(
                information.userSessionId ==
                userSessions[userIndex],
                "Requests from one user have different sessions"
            );

            check(
                information.userSocket ==
                userSockets[userIndex],
                "Requests from one user have different sockets"
            );

            uint8_t expectedData[4] = {};

            buildRequestData(
                userIndex,
                expectedClientId,
                expectedData
            );

            check(
                std::memcmp(
                    data,
                    expectedData,
                    sizeof(data)
                ) == 0,
                "Stored request payload was corrupted"
            );

            ++expectedIds[userIndex];
        }

        // Verify all priorities and their FIFO heads.
        for (std::size_t i = 0;
            i < USER_COUNT;
            ++i)
        {
            check(
                expectedIds[i] ==
                REQUESTS_PER_USER + 1,
                "A user request is missing"
            );

            check(
                system.requests.getQueueSize(
                    PRIORITIES[i]
                ) == REQUESTS_PER_USER,
                "Incorrect priority queue size"
            );

            uint64_t queueHead = 0;

            check(
                system.requests.peekNext(
                    PRIORITIES[i],
                    queueHead
                ) && queueHead == firstLbIds[i],
                "Incorrect priority queue head"
            );

            for (std::size_t j = 0; j < i; ++j)
            {
                check(
                    userSessions[i] != userSessions[j],
                    "Different users share a session"
                );

                check(
                    userSockets[i] != userSockets[j],
                    "Different users share a socket"
                );
            }
        }
    }

    // Verifies worker registration and heartbeat information.
    void verifyWorkers(
        ReceiverSystem& system,
        uint64_t* workerSessions
    )
    {
        check(
            system.workers.size() == WORKER_COUNT &&
            system.workers.getActiveWorkerCount() ==
            WORKER_COUNT &&
            system.workers.getFreeWorkerCount() ==
            WORKER_COUNT &&
            system.workers.getFreeQueueSize() ==
            WORKER_COUNT,
            "Incorrect Worker Registry state"
        );

        for (std::size_t i = 0;
            i < WORKER_COUNT;
            ++i)
        {
            const uint64_t workerId = i + 1;

            const WorkerEntry worker =
                getWorker(
                    system.workers,
                    workerId
                );

            check(
                worker.sessionId != 0 &&
                worker.workerSocket != INVALID_SOCKET &&
                worker.status == WorkerStatus::FREE &&
                worker.currentRequestId == 0,
                "Incorrect free worker state"
            );

            workerSessions[i] =
                worker.sessionId;

            TwrWorkerInfo diagnostic;

            check(
                system.twr.getWorker(
                    workerId,
                    diagnostic),
                "TWR worker information is missing"
            );

            check(
                diagnostic.sessionId == worker.sessionId &&
                diagnostic.observedHeartbeats ==
                HEARTBEATS_PER_WORKER &&
                diagnostic.observedResults == 0 &&
                diagnostic.lastHeartbeatAt ==
                worker.lastHeartbeatMs,
                "Incorrect worker heartbeat information"
            );

            check(
                system.workers.isFreeWorker(
                    createToken(
                        workerId,
                        worker.sessionId)),
                "Registered worker is not free"
            );

            for (std::size_t j = 0; j < i; ++j)
            {
                check(
                    workerSessions[i] != workerSessions[j],
                    "Workers share a session identifier"
                );
            }
        }
    }

    // Tests the complete receiver subsystem.
    void testReceiverSystem()
    {
        ReceiverSystem system;

        TcpClient users[USER_COUNT];
        TcpClient workers[WORKER_COUNT];

        TcpClient duplicateWorker;

        uint64_t userSessions[USER_COUNT] = {};
        uint64_t workerSessions[WORKER_COUNT] = {};

        startSystem(system);

        // STEP 1: Register three users and three workers.
        for (std::size_t i = 0;
            i < USER_COUNT;
            ++i)
        {
            connectUser(system, users[i]);

            connectWorker(
                system,
                workers[i],
                i + 1
            );
        }

        waitUntil(
            system,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.activeUsers == USER_COUNT &&
                    tur.pendingConnections == 0 &&
                    tur.handedOffWorkers == WORKER_COUNT &&
                    twr.activeWorkers == WORKER_COUNT;
            }
        );

        std::cout
            << "[PASS] Mixed user and worker registration\n";

        // STEP 2: Send messages concurrently.
        sendConcurrentMessages(users, workers);

        waitUntil(
            system,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.observedRequests ==
                    TOTAL_REQUESTS &&
                    twr.observedHeartbeats ==
                    TOTAL_HEARTBEATS;
            }
        );

        check(
            system.tur.getStatistics().rejectedConnections == 0 &&
            system.twr.getStatistics().rejectedWorkers == 0,
            "Unexpected connection rejection"
        );

        verifyRequests(system, userSessions);

        verifyWorkers(system, workerSessions);

        checkSemaphoreCount(
            system.requests.getRequestsSemaphore(),
            static_cast<LONG>(TOTAL_REQUESTS)
        );

        checkSemaphoreCount(
            system.workers.getFreeWorkersSemaphore(),
            static_cast<LONG>(WORKER_COUNT)
        );

        std::cout
            << "       Registered requests: "
            << TOTAL_REQUESTS << "\n"
            << "       Received heartbeats: "
            << TOTAL_HEARTBEATS << "\n"
            << "       QH: 200 | QM: 200 | QL: 200\n"
            << "[PASS] Concurrent reception and registry consistency\n";

        // STEP 3: Reject a duplicate active worker.
        connectWorker(
            system,
            duplicateWorker,
            1
        );

        waitUntil(
            system,
            [](const TurStatistics&,
                const TwrStatistics& twr)
            {
                return twr.rejectedWorkers == 1;
            }
        );

        expectPeerClosed(duplicateWorker);

        check(
            getWorker(
                system.workers, 1
            ).sessionId == workerSessions[0],
            "Duplicate worker replaced the original session"
        );

        check(
            system.workers.getFreeQueueSize() ==
            WORKER_COUNT &&
            system.workers.getActiveWorkerCount() ==
            WORKER_COUNT,
            "Duplicate registration changed the worker state"
        );

        checkResult(
            duplicateWorker.disconnect(),
            "Duplicate worker cleanup failed"
        );

        std::cout
            << "[PASS] Duplicate worker isolation\n";

        // STEP 4: Disconnect one user and one worker.
        checkResult(
            users[0].disconnect(),
            "User disconnection failed"
        );

        checkResult(
            workers[0].disconnect(),
            "Worker disconnection failed"
        );

        waitUntil(
            system,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.activeUsers ==
                    USER_COUNT - 1 &&
                    twr.activeWorkers ==
                    WORKER_COUNT - 1;
            }
        );

        std::size_t disconnectedRequests = 0;

        for (uint64_t id = 1;
            id <= TOTAL_REQUESTS;
            ++id)
        {
            const RegisteredRequestInfo request =
                getRequest(system.requests, id);

            const bool belongsToOldUser =
                request.userSessionId == userSessions[0];

            check(
                request.userConnected != belongsToOldUser,
                "Incorrect user disconnection state"
            );

            check(
                request.status == RequestStatus::QUEUED,
                "Disconnection changed the request status"
            );

            if (belongsToOldUser)
            {
                ++disconnectedRequests;
            }
        }

        check(
            disconnectedRequests == REQUESTS_PER_USER,
            "Incorrect disconnected request count"
        );

        check(
            system.requests.size() == TOTAL_REQUESTS,
            "Disconnection removed accepted requests"
        );

        const WorkerEntry dead =
            getWorker(system.workers, 1);

        check(
            dead.status == WorkerStatus::DEAD &&
            dead.workerSocket == INVALID_SOCKET &&
            dead.sessionId == workerSessions[0],
            "Disconnected worker was not marked DEAD"
        );

        check(
            system.workers.getFreeWorkerCount() ==
            WORKER_COUNT - 1 &&
            system.workers.getFreeQueueSize() ==
            WORKER_COUNT,
            "Incorrect stale worker queue state"
        );

        std::cout
            << "[PASS] User and worker disconnection isolation\n";

        // STEP 5: Reconnect both clients.
        connectUser(system, users[0]);

        connectWorker(system, workers[0], 1);

        // Reuse the original client request ID in a new session.
        sendRequest(users[0], 0, 1);

        HeartbeatPayload heartbeat;

        sendChecked(workers[0], heartbeat);

        waitUntil(
            system,
            [](const TurStatistics& tur,
                const TwrStatistics& twr)
            {
                return tur.activeUsers == USER_COUNT &&
                    twr.activeWorkers == WORKER_COUNT &&
                    tur.observedRequests ==
                    TOTAL_REQUESTS + 1 &&
                    twr.observedHeartbeats ==
                    TOTAL_HEARTBEATS + 1;
            }
        );

        const RegisteredRequestInfo newRequest =
            getRequest(
                system.requests,
                TOTAL_REQUESTS + 1
            );

        check(
            newRequest.clientRequestId == 1 &&
            newRequest.userSessionId != userSessions[0] &&
            newRequest.userSessionId != 0 &&
            newRequest.userConnected &&
            newRequest.priority == Priority::HIGH,
            "Incorrect replacement user session"
        );

        for (uint64_t id = 1;
            id <= TOTAL_REQUESTS;
            ++id)
        {
            const RegisteredRequestInfo request =
                getRequest(system.requests, id);

            check(
                request.userConnected ==
                (request.userSessionId != userSessions[0]),
                "Reconnection changed an old user session"
            );
        }

        const WorkerEntry replacement =
            getWorker(system.workers, 1);

        check(
            replacement.sessionId != workerSessions[0] &&
            replacement.status == WorkerStatus::FREE,
            "Incorrect replacement worker session"
        );

        check(
            !system.workers.isFreeWorker(
                createToken(1, workerSessions[0])),
            "Old worker token became valid again"
        );

        check(
            system.workers.isFreeWorker(
                createToken(1, replacement.sessionId)),
            "Replacement worker token is invalid"
        );

        WorkerEntry previousState;

        check(
            !system.workers.markDead(
                1,
                workerSessions[0],
                previousState),
            "Old disconnection killed the replacement worker"
        );

        check(
            !system.workers.updateHeartbeat(
                1,
                workerSessions[0],
                GetTickCount64()),
            "Old heartbeat affected the replacement worker"
        );

        check(
            system.requests.size() == TOTAL_REQUESTS + 1 &&
            system.requests.getQueueSize(
                Priority::HIGH) == REQUESTS_PER_USER + 1 &&
            system.requests.getQueueSize(
                Priority::MEDIUM) == REQUESTS_PER_USER &&
            system.requests.getQueueSize(
                Priority::LOW) == REQUESTS_PER_USER,
            "Incorrect request state after reconnection"
        );

        check(
            system.workers.size() == WORKER_COUNT &&
            system.workers.getActiveWorkerCount() ==
            WORKER_COUNT &&
            system.workers.getFreeWorkerCount() ==
            WORKER_COUNT &&
            system.workers.getFreeQueueSize() ==
            WORKER_COUNT + 1,
            "Incorrect worker state after reconnection"
        );

        checkSemaphoreCount(
            system.requests.getRequestsSemaphore(),
            static_cast<LONG>(TOTAL_REQUESTS + 1)
        );

        checkSemaphoreCount(
            system.workers.getFreeWorkersSemaphore(),
            static_cast<LONG>(WORKER_COUNT + 1)
        );

        std::cout
            << "       Old worker session: "
            << workerSessions[0] << "\n"
            << "       New worker session: "
            << replacement.sessionId << "\n"
            << "[PASS] Reconnection and stale session protection\n";

        // STEP 6: Shut down while all clients are connected.
        stopSystem(system);

        for (std::size_t i = 0;
            i < USER_COUNT;
            ++i)
        {
            expectPeerClosed(users[i]);

            checkResult(
                users[i].disconnect(),
                "Final user socket cleanup failed"
            );
        }

        for (std::size_t i = 0;
            i < WORKER_COUNT;
            ++i)
        {
            expectPeerClosed(workers[i]);

            const WorkerEntry worker =
                getWorker(system.workers, i + 1);

            check(
                worker.status == WorkerStatus::DEAD &&
                worker.workerSocket == INVALID_SOCKET,
                "Shutdown retained an active worker session"
            );

            checkResult(
                workers[i].disconnect(),
                "Final worker socket cleanup failed"
            );
        }

        // All accepted requests must still exist in memory.
        for (uint64_t id = 1;
            id <= TOTAL_REQUESTS + 1;
            ++id)
        {
            const RegisteredRequestInfo request =
                getRequest(system.requests, id);

            check(
                !request.userConnected &&
                !request.responseSent &&
                request.status == RequestStatus::QUEUED,
                "Incorrect final request state"
            );
        }

        check(
            system.requests.size() == TOTAL_REQUESTS + 1,
            "Shutdown unexpectedly removed request records"
        );

        check(
            system.workers.getFreeQueueSize() ==
            WORKER_COUNT + 1,
            "Unexpected final worker queue size"
        );

        checkSemaphoreCount(
            system.requests.getRequestsSemaphore(),
            static_cast<LONG>(TOTAL_REQUESTS + 1)
        );

        checkSemaphoreCount(
            system.workers.getFreeWorkersSemaphore(),
            static_cast<LONG>(WORKER_COUNT + 1)
        );

        std::cout
            << "[PASS] Receiver subsystem shutdown consistency\n";

        std::cout
            << "\n       Final request records: "
            << system.requests.size() << "\n"

            << "       Final worker records: "
            << system.workers.size() << "\n"

            << "       Active workers: "
            << system.workers.getActiveWorkerCount() << "\n"

            << "       Free workers: "
            << system.workers.getFreeWorkerCount() << "\n"

            << "       Physical free queue items: "
            << system.workers.getFreeQueueSize() << "\n"

            << "[PASS] Complete receiver system integration test\n";
    }
}

// Runs the complete receiver subsystem integration test.
void runReceiverSystemIntegrationTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization failed"
    );

    testReceiverSystem();

    checkResult(
        context.cleanup(),
        "Winsock cleanup failed"
    );

    std::cout
        << "All receiver system integration tests passed.\n";
}