#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include "RequestRegistry.h"
#include "Constants.h"
#include "TestSuites.h"

namespace
{
    // Synthetic socket values used only for registry tests.
    constexpr SOCKET USER_SOCKET_1 =
        static_cast<SOCKET>(100);

    constexpr SOCKET USER_SOCKET_2 =
        static_cast<SOCKET>(200);

    constexpr uint64_t USER_SESSION_1 = 1;

    constexpr uint64_t USER_SESSION_2 = 2;

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

    // Creates a request payload without owning its data.
    RequestPayload createRequest(
        uint64_t clientRequestId,
        Priority priority,
        const uint8_t* data,
        uint32_t dataSize
    )
    {
        RequestPayload request;

        request.clientRequestId =
            clientRequestId;

        request.priority = priority;

        request.data = data;

        request.dataSize = dataSize;

        return request;
    }

    // Submits a request and returns its generated ID.
    uint64_t submitChecked(
        RequestRegistry& registry,
        SOCKET userSocket,
        uint64_t userSessionId,
        const RequestPayload& request
    )
    {
        uint64_t lbRequestId = 0;

        checkResult(
            registry.submit(
                userSocket,
                userSessionId,
                request,
                lbRequestId),
            "Request submission failed"
        );

        check(
            lbRequestId != 0,
            "Generated request identifier is invalid"
        );

        return lbRequestId;
    }

    // Returns registered request metadata.
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
            "Registered request identifier mismatch"
        );

        return information;
    }

    // Verifies queue sizes and total registry size.
    void checkSizes(
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
            registry.size() ==
            high + medium + low,
            "Incorrect request registry size"
        );
    }

    // Checks the actual semaphore count.
    // No producers or consumers may be active during this test.
    void checkSemaphoreCount(
        RequestRegistry& registry,
        LONG expectedCount
    )
    {
        HANDLE semaphore =
            registry.getRequestsSemaphore();

        check(
            semaphore != nullptr,
            "Request semaphore is invalid"
        );

        for (LONG i = 0;
            i < expectedCount;
            ++i)
        {
            check(
                WaitForSingleObject(
                    semaphore, 0
                ) == WAIT_OBJECT_0,
                "Semaphore contains too few signals"
            );
        }

        check(
            WaitForSingleObject(
                semaphore, 0
            ) == WAIT_TIMEOUT,
            "Semaphore contains extra signals"
        );

        // Restore all consumed semaphore signals.
        if (expectedCount > 0)
        {
            check(
                ReleaseSemaphore(
                    semaphore,
                    expectedCount,
                    nullptr) != 0,
                "Semaphore signal restoration failed"
            );
        }
    }

    // Tests initialization and invalid capacities.
    void testInitialState()
    {
        RequestRegistry registry;

        check(
            registry.isValid(),
            "Request registry initialization failed"
        );

        checkSizes(registry, 0, 0, 0);

        checkSemaphoreCount(registry, 0);

        RegisteredRequestInfo information;

        check(
            !registry.getRequest(1, information),
            "Empty registry returned a request"
        );

        uint64_t lbRequestId = 0;

        check(
            !registry.peekNext(
                Priority::HIGH,
                lbRequestId),
            "Empty queue returned a request"
        );

        RequestRegistry invalidZero(0);

        check(
            !invalidZero.isValid(),
            "Zero registry capacity was accepted"
        );

        RequestRegistry invalidLarge(
            RequestRegistry::MAX_REQUESTS + 1
        );

        check(
            !invalidLarge.isValid(),
            "Invalid registry capacity was accepted"
        );

        std::cout
            << "[PASS] Initial request registry state test\n";
    }

    // Tests registration and initial request metadata.
    void testRequestRegistration()
    {
        RequestRegistry registry;

        const uint8_t data[] =
        {
            10, 20, 30, 40
        };

        const RequestPayload request =
            createRequest(
                100,
                Priority::HIGH,
                data,
                sizeof(data)
            );

        const uint64_t lbRequestId =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );

        check(
            lbRequestId == 1,
            "Incorrect first request identifier"
        );

        const RegisteredRequestInfo information =
            getRequest(registry, lbRequestId);

        check(
            information.clientRequestId == 100,
            "Original client identifier was lost"
        );

        check(
            information.userSocket == USER_SOCKET_1,
            "Incorrect registered user socket"
        );

        check(
            information.userSessionId ==
            USER_SESSION_1,
            "Incorrect registered user session"
        );

        check(
            information.priority == Priority::HIGH,
            "Incorrect registered request priority"
        );

        check(
            information.status ==
            RequestStatus::QUEUED,
            "Incorrect initial request status"
        );

        check(
            information.attemptId == 0,
            "Initial request attempt is invalid"
        );

        check(
            information.assignedWorkerId == 0,
            "New request already has a worker"
        );

        check(
            !information.responseSent,
            "New request already has a response"
        );

        check(
            information.userConnected,
            "New request user is disconnected"
        );

        check(
            information.dataSize == sizeof(data),
            "Incorrect stored request data size"
        );

        checkSizes(registry, 1, 0, 0);

        checkSemaphoreCount(registry, 1);

        std::cout
            << "[PASS] Request registration metadata test\n";
    }

    // Tests priority routing and independent FIFO queues.
    void testPriorityQueues()
    {
        RequestRegistry registry;

        const uint8_t data[] = { 1, 2, 3 };

        const Priority priorities[] =
        {
            Priority::HIGH,
            Priority::LOW,
            Priority::HIGH,
            Priority::MEDIUM,
            Priority::LOW
        };

        for (uint64_t i = 0; i < 5; ++i)
        {
            const RequestPayload request =
                createRequest(
                    i + 1,
                    priorities[i],
                    data,
                    sizeof(data)
                );

            const uint64_t lbRequestId =
                submitChecked(
                    registry,
                    USER_SOCKET_1,
                    USER_SESSION_1,
                    request
                );

            check(
                lbRequestId == i + 1,
                "Request identifiers are not sequential"
            );
        }

        checkSizes(registry, 2, 1, 2);

        uint64_t first = 0;

        check(
            registry.peekNext(
                Priority::HIGH, first
            ) && first == 1,
            "Incorrect first high priority request"
        );

        check(
            registry.peekNext(
                Priority::MEDIUM, first
            ) && first == 4,
            "Incorrect first medium priority request"
        );

        check(
            registry.peekNext(
                Priority::LOW, first
            ) && first == 2,
            "Incorrect first low priority request"
        );

        // Peeking must not remove requests or semaphore signals.
        checkSizes(registry, 2, 1, 2);

        checkSemaphoreCount(registry, 5);

        std::cout
            << "       QH: 2 | QM: 1 | QL: 2\n";

        std::cout
            << "[PASS] Request priority queues test\n";
    }

    // Tests owned request data and safe copying.
    void testDataOwnership()
    {
        RequestRegistry registry;

        uint8_t input[] =
        {
            10, 0, 20, 255
        };

        const uint8_t expected[] =
        {
            10, 0, 20, 255
        };

        const RequestPayload request =
            createRequest(
                1,
                Priority::MEDIUM,
                input,
                sizeof(input)
            );

        const uint64_t lbRequestId =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );

        // Simulate reuse of the network receive buffer.
        std::memset(
            input,
            0,
            sizeof(input)
        );

        uint8_t output[sizeof(expected)] = {};

        uint32_t bytesCopied = 0;

        check(
            registry.copyRequestData(
                lbRequestId,
                output,
                sizeof(output),
                bytesCopied),
            "Stored request data copy failed"
        );

        check(
            bytesCopied == sizeof(expected),
            "Incorrect copied request data size"
        );

        check(
            std::memcmp(
                output,
                expected,
                sizeof(expected)
            ) == 0,
            "Stored request data depends on source buffer"
        );

        uint8_t smallBuffer[] = { 77, 88 };

        check(
            !registry.copyRequestData(
                lbRequestId,
                smallBuffer,
                sizeof(smallBuffer),
                bytesCopied),
            "Insufficient output capacity was accepted"
        );

        check(
            bytesCopied == 0 &&
            smallBuffer[0] == 77 &&
            smallBuffer[1] == 88,
            "Failed copy modified the output buffer"
        );

        std::cout
            << "[PASS] Request data ownership test\n";
    }

    // Tests equal client IDs from different users.
    void testIndependentClientIds()
    {
        RequestRegistry registry;

        const RequestPayload request =
            createRequest(
                1,
                Priority::HIGH,
                nullptr,
                0
            );

        const uint64_t first =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );

        const uint64_t second =
            submitChecked(
                registry,
                USER_SOCKET_2,
                USER_SESSION_2,
                request
            );

        check(
            first != second,
            "Different users received the same internal ID"
        );

        const RegisteredRequestInfo firstInfo =
            getRequest(registry, first);

        const RegisteredRequestInfo secondInfo =
            getRequest(registry, second);

        check(
            firstInfo.clientRequestId ==
            secondInfo.clientRequestId,
            "Original client request IDs were modified"
        );

        check(
            firstInfo.userSessionId !=
            secondInfo.userSessionId,
            "Independent user sessions were mixed"
        );

        uint32_t bytesCopied = 100;

        check(
            registry.copyRequestData(
                first,
                nullptr,
                0,
                bytesCopied) &&
            bytesCopied == 0,
            "Empty request data copy failed"
        );

        checkSizes(registry, 2, 0, 0);

        checkSemaphoreCount(registry, 2);

        std::cout
            << "[PASS] Independent client request IDs test\n";
    }

    // Tests invalid request submission without state changes.
    void testInvalidRequests()
    {
        RequestRegistry registry;

        RequestPayload request =
            createRequest(
                1,
                Priority::HIGH,
                nullptr,
                0
            );

        uint64_t lbRequestId = 999;

        check(
            registry.submit(
                INVALID_SOCKET,
                USER_SESSION_1,
                request,
                lbRequestId
            ) == WSAEINVAL &&
            lbRequestId == 0,
            "Invalid user socket was accepted"
        );

        check(
            registry.submit(
                USER_SOCKET_1,
                0,
                request,
                lbRequestId
            ) == WSAEINVAL,
            "Invalid user session was accepted"
        );

        request.priority =
            static_cast<Priority>(255);

        check(
            registry.submit(
                USER_SOCKET_1,
                USER_SESSION_1,
                request,
                lbRequestId
            ) == WSAEINVAL,
            "Invalid priority was accepted"
        );

        request.priority = Priority::LOW;

        request.dataSize = 10;

        check(
            registry.submit(
                USER_SOCKET_1,
                USER_SESSION_1,
                request,
                lbRequestId
            ) == WSAEINVAL,
            "Null request data was accepted"
        );

        uint8_t byte = 1;

        request.data = &byte;

        request.dataSize =
            Constants::MAX_REQUEST_DATA_SIZE + 1;

        check(
            registry.submit(
                USER_SOCKET_1,
                USER_SESSION_1,
                request,
                lbRequestId
            ) == WSAEINVAL,
            "Oversized request data was accepted"
        );

        checkSizes(registry, 0, 0, 0);

        checkSemaphoreCount(registry, 0);

        request.data = &byte;
        request.dataSize = 1;

        check(
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            ) == 1,
            "Invalid submissions consumed request identifiers"
        );

        checkSizes(registry, 0, 0, 1);

        checkSemaphoreCount(registry, 1);

        std::cout
            << "[PASS] Invalid request rejection test\n";
    }

    // Tests disconnected user sessions and socket reuse.
    void testUserDisconnection()
    {
        RequestRegistry registry;

        const uint8_t data[] = { 10, 20 };

        const RequestPayload request =
            createRequest(
                1,
                Priority::HIGH,
                data,
                sizeof(data)
            );

        const uint64_t oldFirst =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );

        const uint64_t oldSecond =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );

        check(
            registry.markUserDisconnected(
                USER_SESSION_1
            ) == 2,
            "Disconnected session requests were not updated"
        );

        // Simulate reuse of the old socket handle.
        const uint64_t newRequest =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_2,
                request
            );

        const RegisteredRequestInfo first =
            getRequest(registry, oldFirst);

        const RegisteredRequestInfo second =
            getRequest(registry, oldSecond);

        const RegisteredRequestInfo current =
            getRequest(registry, newRequest);

        check(
            !first.userConnected &&
            !second.userConnected,
            "Old session remained connected"
        );

        check(
            current.userConnected,
            "New user session was disconnected"
        );

        check(
            first.status ==
            RequestStatus::QUEUED &&
            second.status ==
            RequestStatus::QUEUED,
            "Disconnected user requests were cancelled"
        );

        check(
            !first.responseSent &&
            !second.responseSent,
            "Disconnection incorrectly marked responses as sent"
        );

        check(
            registry.markUserDisconnected(
                USER_SESSION_1
            ) == 0,
            "Old session disconnection was applied twice"
        );

        check(
            getRequest(
                registry,
                newRequest
            ).userConnected,
            "Old session affected a reused socket"
        );

        checkSizes(registry, 3, 0, 0);

        checkSemaphoreCount(registry, 3);

        std::cout
            << "[PASS] User disconnection and socket reuse test\n";
    }

    // Tests registry capacity and semaphore consistency.
    void testCapacity()
    {
        RequestRegistry registry(3);

        const RequestPayload request =
            createRequest(
                1,
                Priority::LOW,
                nullptr,
                0
            );

        for (int i = 0; i < 3; ++i)
        {
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );
        }

        uint64_t lbRequestId = 0;

        check(
            registry.submit(
                USER_SOCKET_1,
                USER_SESSION_1,
                request,
                lbRequestId
            ) == WSAENOBUFS,
            "Full request registry accepted another request"
        );

        check(
            lbRequestId == 0,
            "Rejected request received an internal ID"
        );

        check(
            registry.isValid(),
            "Capacity rejection corrupted the registry"
        );

        checkSizes(registry, 0, 0, 3);

        checkSemaphoreCount(registry, 3);

        std::cout
            << "[PASS] Request registry capacity test\n";
    }

    // Tests storage of the maximum request data size.
    void testMaximumData()
    {
        RequestRegistry registry;

        static uint8_t input[
            Constants::MAX_REQUEST_DATA_SIZE
        ];

        static uint8_t output[
            Constants::MAX_REQUEST_DATA_SIZE
        ];

        for (uint32_t i = 0;
            i < Constants::MAX_REQUEST_DATA_SIZE;
            ++i)
        {
            input[i] =
                static_cast<uint8_t>(i % 256);
        }

        const RequestPayload request =
            createRequest(
                10,
                Priority::MEDIUM,
                input,
                Constants::MAX_REQUEST_DATA_SIZE
            );

        const uint64_t lbRequestId =
            submitChecked(
                registry,
                USER_SOCKET_1,
                USER_SESSION_1,
                request
            );

        uint32_t bytesCopied = 0;

        check(
            registry.copyRequestData(
                lbRequestId,
                output,
                sizeof(output),
                bytesCopied),
            "Maximum request data copy failed"
        );

        check(
            bytesCopied ==
            Constants::MAX_REQUEST_DATA_SIZE,
            "Incorrect maximum stored data size"
        );

        check(
            std::memcmp(
                input,
                output,
                sizeof(input)
            ) == 0,
            "Maximum request data was corrupted"
        );

        checkSemaphoreCount(registry, 1);

        std::cout
            << "       Stored bytes: "
            << bytesCopied << "\n";

        std::cout
            << "[PASS] Maximum request data test\n";
    }

    constexpr std::size_t REQUESTS_PER_THREAD = 500;

    // Contains parameters for a concurrent request producer.
    struct ProducerTask
    {
        RequestRegistry* registry = nullptr;

        SOCKET userSocket = INVALID_SOCKET;

        uint64_t userSessionId = 0;

        Priority priority = Priority::HIGH;

        uint64_t ids[REQUESTS_PER_THREAD] = {};

        int result = 0;
    };

    // Submits requests concurrently.
    unsigned __stdcall producerThread(void* parameter)
    {
        ProducerTask* task =
            static_cast<ProducerTask*>(parameter);

        for (std::size_t i = 0;
            i < REQUESTS_PER_THREAD;
            ++i)
        {
            const uint8_t data[] =
            {
                static_cast<uint8_t>(
                    task->userSessionId
                ),
                static_cast<uint8_t>(i % 256)
            };

            const RequestPayload request =
                createRequest(
                    i + 1,
                    task->priority,
                    data,
                    sizeof(data)
                );

            task->result =
                task->registry->submit(
                    task->userSocket,
                    task->userSessionId,
                    request,
                    task->ids[i]
                );

            if (task->result != 0)
            {
                return 1;
            }
        }

        return 0;
    }

    // Starts a request producer thread.
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

        if (handle == 0)
        {
            std::cerr
                << "[FAIL] Producer thread creation failed\n";

            std::abort();
        }

        return reinterpret_cast<HANDLE>(handle);
    }

    // Waits for a request producer thread.
    void joinProducer(HANDLE thread)
    {
        check(
            WaitForSingleObject(
                thread, 10000
            ) == WAIT_OBJECT_0,
            "Producer thread did not finish"
        );

        check(
            CloseHandle(thread) != 0,
            "Producer thread handle cleanup failed"
        );
    }

    // Tests synchronized registration from two threads.
    void testConcurrentRegistration()
    {
        RequestRegistry registry;

        ProducerTask first;

        first.registry = &registry;

        first.userSocket = USER_SOCKET_1;

        first.userSessionId = USER_SESSION_1;

        first.priority = Priority::HIGH;

        ProducerTask second;

        second.registry = &registry;

        second.userSocket = USER_SOCKET_2;

        second.userSessionId = USER_SESSION_2;

        second.priority = Priority::LOW;

        HANDLE firstThread =
            startProducer(first);

        HANDLE secondThread =
            startProducer(second);

        joinProducer(firstThread);

        joinProducer(secondThread);

        checkResult(
            first.result,
            "First request producer failed"
        );

        checkResult(
            second.result,
            "Second request producer failed"
        );

        constexpr std::size_t TOTAL_REQUESTS =
            REQUESTS_PER_THREAD * 2;

        bool seen[TOTAL_REQUESTS + 1] = {};

        ProducerTask* tasks[] =
        {
            &first,
            &second
        };

        // Verify every generated ID and its associated data.
        for (int t = 0; t < 2; ++t)
        {
            ProducerTask& task = *tasks[t];

            for (std::size_t i = 0;
                i < REQUESTS_PER_THREAD;
                ++i)
            {
                const uint64_t id = task.ids[i];

                check(
                    id > 0 &&
                    id <= TOTAL_REQUESTS,
                    "Concurrent request identifier is invalid"
                );

                check(
                    !seen[id],
                    "Duplicate internal request identifier"
                );

                seen[id] = true;

                const RegisteredRequestInfo information =
                    getRequest(registry, id);

                check(
                    information.clientRequestId ==
                    i + 1 &&
                    information.userSessionId ==
                    task.userSessionId &&
                    information.userSocket ==
                    task.userSocket &&
                    information.priority ==
                    task.priority,
                    "Concurrent request metadata was corrupted"
                );

                uint8_t output[2] = {};

                uint32_t bytesCopied = 0;

                check(
                    registry.copyRequestData(
                        id,
                        output,
                        sizeof(output),
                        bytesCopied),
                    "Concurrent request data copy failed"
                );

                check(
                    bytesCopied == 2 &&
                    output[0] ==
                    task.userSessionId &&
                    output[1] ==
                    static_cast<uint8_t>(i % 256),
                    "Concurrent request data was corrupted"
                );
            }
        }

        for (std::size_t i = 1;
            i <= TOTAL_REQUESTS;
            ++i)
        {
            check(
                seen[i],
                "Concurrent request identifier is missing"
            );
        }

        checkSizes(
            registry,
            REQUESTS_PER_THREAD,
            0,
            REQUESTS_PER_THREAD
        );

        checkSemaphoreCount(
            registry,
            static_cast<LONG>(TOTAL_REQUESTS)
        );

        check(
            registry.getFatalError() == 0,
            "Registry reported an internal failure"
        );

        std::cout
            << "       Registered requests: "
            << TOTAL_REQUESTS << "\n"
            << "       Unique identifiers: "
            << TOTAL_REQUESTS << "\n"
            << "       Semaphore signals: "
            << TOTAL_REQUESTS << "\n";

        std::cout
            << "[PASS] Concurrent request registration test\n";
    }
}

// Runs all request registry tests.
void runRequestRegistryTests()
{
    testInitialState();

    testRequestRegistration();

    testPriorityQueues();

    testDataOwnership();

    testIndependentClientIds();

    testInvalidRequests();

    testUserDisconnection();

    testCapacity();

    testMaximumData();

    testConcurrentRegistration();

    std::cout
        << "All request registry tests passed.\n";
}