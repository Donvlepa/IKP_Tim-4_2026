#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "MessageIO.h"
#include "PayloadCodec.h"
#include "Constants.h"
#include "TestSuites.h"

namespace
{
    constexpr uint32_t TEST_TIMEOUT_MS = 5000;
    constexpr uint32_t CONCURRENT_MESSAGE_COUNT = 32;

    // Represents one client connection to the load balancer.
    struct IntegrationConnection
    {
        SOCKET client = INVALID_SOCKET;
        SOCKET server = INVALID_SOCKET;
    };

    // Simulates one load balancer, one user and one worker.
    struct IntegrationNetwork
    {
        SOCKET listener = INVALID_SOCKET;

        IntegrationConnection user;
        IntegrationConnection worker;
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

    // Checks a Winsock operation result.
    void checkResult(int result, const char* message)
    {
        if (result != 0)
        {
            std::cerr << "[FAIL] " << message
                << " | Winsock error: "
                << result << "\n";

            std::abort();
        }
    }

    // Waits until a socket becomes readable.
    void waitReadable(
        SOCKET socketHandle,
        uint32_t timeoutMs
    )
    {
        fd_set readSet;

        FD_ZERO(&readSet);
        FD_SET(socketHandle, &readSet);

        timeval timeout = {};

        timeout.tv_sec =
            static_cast<long>(timeoutMs / 1000);

        timeout.tv_usec =
            static_cast<long>((timeoutMs % 1000) * 1000);

        const int result = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        );

        if (result == SOCKET_ERROR)
        {
            checkResult(
                WSAGetLastError(),
                "Socket selection failed"
            );
        }

        check(
            result == 1 &&
            FD_ISSET(socketHandle, &readSet),
            "Socket readiness timed out"
        );
    }

    // Connects one client to the test load balancer.
    void connectClient(
        SOCKET listener,
        uint16_t port,
        IntegrationConnection& connection
    )
    {
        checkResult(
            SocketUtils::createTcpSocket(
                connection.client),
            "Client socket creation failed"
        );

        checkResult(
            SocketUtils::connectSocket(
                connection.client,
                "127.0.0.1",
                port),
            "Client connection failed"
        );

        waitReadable(listener, TEST_TIMEOUT_MS);

        checkResult(
            SocketUtils::acceptSocket(
                listener,
                connection.server),
            "Client acceptance failed"
        );

        checkResult(
            SocketUtils::setNonBlocking(
                connection.client, true),
            "Client non-blocking configuration failed"
        );

        checkResult(
            SocketUtils::setNonBlocking(
                connection.server, true),
            "Server non-blocking configuration failed"
        );
    }

    // Creates the complete test network.
    void createNetwork(IntegrationNetwork& network)
    {
        checkResult(
            SocketUtils::createTcpSocket(
                network.listener),
            "Listener creation failed"
        );

        checkResult(
            SocketUtils::bindSocket(
                network.listener,
                "127.0.0.1",
                0),
            "Listener binding failed"
        );

        checkResult(
            SocketUtils::listenSocket(
                network.listener),
            "Listener setup failed"
        );

        uint16_t port = 0;

        checkResult(
            SocketUtils::getLocalPort(
                network.listener,
                port),
            "Local port retrieval failed"
        );

        check(port != 0,
            "Invalid local port");

        connectClient(
            network.listener,
            port,
            network.user
        );

        connectClient(
            network.listener,
            port,
            network.worker
        );

        check(
            network.user.server != network.worker.server,
            "User and worker share the same socket"
        );
    }

    // Closes one client connection.
    void closeConnection(
        IntegrationConnection& connection
    )
    {
        checkResult(
            SocketUtils::closeSocket(
                connection.client),
            "Client socket cleanup failed"
        );

        checkResult(
            SocketUtils::closeSocket(
                connection.server),
            "Server socket cleanup failed"
        );
    }

    // Releases all network sockets.
    void closeNetwork(IntegrationNetwork& network)
    {
        closeConnection(network.user);
        closeConnection(network.worker);

        checkResult(
            SocketUtils::closeSocket(
                network.listener),
            "Listener cleanup failed"
        );
    }

    // Sends one complete protocol message.
    template<typename Payload>
    void sendChecked(
        SOCKET socketHandle,
        const Payload& payload
    )
    {
        std::size_t bytesSent = 0;

        checkResult(
            MessageIO::sendMessage(
                socketHandle,
                payload,
                bytesSent,
                TEST_TIMEOUT_MS),
            "Protocol message sending failed"
        );

        check(
            bytesSent >= Constants::HEADER_SIZE,
            "Incomplete protocol header was sent"
        );
    }

    // Receives one complete protocol message.
    const uint8_t* receiveExpected(
        MessageReceiver& receiver,
        SOCKET socketHandle,
        MessageType expectedType,
        MessageHeader& header
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

            if (status == ReceiveStatus::MessageReady)
            {
                const uint8_t* payload = nullptr;

                check(
                    receiver.getMessage(header, payload),
                    "Complete message retrieval failed"
                );

                check(
                    header.type == expectedType,
                    "Unexpected protocol message type"
                );

                check(
                    header.version ==
                    Constants::PROTOCOL_VERSION,
                    "Unexpected protocol version"
                );

                return payload;
            }

            if (status == ReceiveStatus::SocketError)
            {
                checkResult(
                    socketError,
                    "Message reception failed"
                );
            }

            check(
                status == ReceiveStatus::WouldBlock,
                "Unexpected terminal reception status"
            );

            const ULONGLONG elapsed =
                GetTickCount64() - startTime;

            check(
                elapsed < TEST_TIMEOUT_MS,
                "Integration message reception timed out"
            );

            const uint32_t remaining =
                TEST_TIMEOUT_MS -
                static_cast<uint32_t>(elapsed);

            waitReadable(socketHandle, remaining);
        }
    }

    // Consumes a successfully received message.
    void consumeChecked(MessageReceiver& receiver)
    {
        check(
            receiver.consumeMessage(),
            "Message consumption failed"
        );
    }

    // Starts a test thread.
    HANDLE startThread(
        unsigned(__stdcall* entryPoint)(void*),
        void* parameter
    )
    {
        const uintptr_t threadHandle =
            _beginthreadex(
                nullptr,
                0,
                entryPoint,
                parameter,
                0,
                nullptr
            );

        if (threadHandle == 0)
        {
            std::cerr
                << "[FAIL] Test thread creation failed\n";

            std::abort();
        }

        return reinterpret_cast<HANDLE>(
            threadHandle
            );
    }

    // Waits for a test thread and closes its handle.
    void joinThread(HANDLE threadHandle)
    {
        const DWORD result =
            WaitForSingleObject(
                threadHandle,
                10000
            );

        check(
            result == WAIT_OBJECT_0,
            "Test thread did not finish"
        );

        check(
            CloseHandle(threadHandle) != 0,
            "Thread handle cleanup failed"
        );
    }

    // Tests the complete UR-LB-WR communication flow.
    void testCompleteCommunicationFlow()
    {
        IntegrationNetwork network;

        createNetwork(network);

        MessageReceiver lbUserReceiver;
        MessageReceiver lbWorkerReceiver;
        MessageReceiver userReceiver;
        MessageReceiver workerReceiver;

        MessageHeader header = {};

        // STEP 1: Register the user.

        RegisterUrPayload userRegistration;

        sendChecked(
            network.user.client,
            userRegistration
        );

        const uint8_t* payload =
            receiveExpected(
                lbUserReceiver,
                network.user.server,
                MessageType::REGISTER_UR,
                header
            );

        RegisterUrPayload decodedUserRegistration;

        check(
            PayloadCodec::deserializeRegisterUr(
                payload,
                header.length,
                decodedUserRegistration),
            "User registration decoding failed"
        );

        consumeChecked(lbUserReceiver);

        std::cout
            << "       UR -> LB: REGISTER_UR\n";

        // STEP 2: Register the worker.

        RegisterWrPayload workerRegistration;

        workerRegistration.workerId = 10;

        sendChecked(
            network.worker.client,
            workerRegistration
        );

        payload = receiveExpected(
            lbWorkerReceiver,
            network.worker.server,
            MessageType::REGISTER_WR,
            header
        );

        RegisterWrPayload decodedWorkerRegistration;

        check(
            PayloadCodec::deserializeRegisterWr(
                payload,
                header.length,
                decodedWorkerRegistration),
            "Worker registration decoding failed"
        );

        check(
            decodedWorkerRegistration.workerId == 10,
            "Incorrect registered worker ID"
        );

        consumeChecked(lbWorkerReceiver);

        std::cout
            << "       WR -> LB: REGISTER_WR\n";

        // STEP 3: Send a user request.

        const uint8_t requestData[] =
        {
            10, 20, 30, 40, 50
        };

        RequestPayload request;

        request.clientRequestId = 100;
        request.priority = Priority::HIGH;

        request.dataSize =
            static_cast<uint32_t>(
                sizeof(requestData)
                );

        request.data = requestData;

        sendChecked(
            network.user.client,
            request
        );

        payload = receiveExpected(
            lbUserReceiver,
            network.user.server,
            MessageType::REQUEST,
            header
        );

        RequestPayload decodedRequest;

        check(
            PayloadCodec::deserializeRequest(
                payload,
                header.length,
                decodedRequest),
            "User request decoding failed"
        );

        check(
            decodedRequest.clientRequestId == 100,
            "Incorrect client request ID"
        );

        check(
            decodedRequest.priority == Priority::HIGH,
            "Incorrect request priority"
        );

        check(
            decodedRequest.dataSize ==
            sizeof(requestData),
            "Incorrect request data size"
        );

        check(
            std::memcmp(
                decodedRequest.data,
                requestData,
                sizeof(requestData)
            ) == 0,
            "Incorrect request data"
        );

        const uint64_t originalClientRequestId =
            decodedRequest.clientRequestId;

        std::cout
            << "       UR -> LB: REQUEST\n";

        // STEP 4: Assign the request to the worker.
        // The identifiers are simulated for this test.

        AssignPayload assignment;

        assignment.lbRequestId = 1000;
        assignment.attemptId = 1;

        assignment.priority =
            decodedRequest.priority;

        assignment.dataSize =
            decodedRequest.dataSize;

        assignment.data =
            decodedRequest.data;

        sendChecked(
            network.worker.server,
            assignment
        );

        // The source payload view is no longer needed.
        consumeChecked(lbUserReceiver);

        payload = receiveExpected(
            workerReceiver,
            network.worker.client,
            MessageType::ASSIGN,
            header
        );

        AssignPayload decodedAssignment;

        check(
            PayloadCodec::deserializeAssign(
                payload,
                header.length,
                decodedAssignment),
            "Assignment decoding failed"
        );

        check(
            decodedAssignment.lbRequestId == 1000,
            "Incorrect internal request ID"
        );

        check(
            decodedAssignment.attemptId == 1,
            "Incorrect assignment attempt ID"
        );

        check(
            decodedAssignment.priority ==
            Priority::HIGH,
            "Incorrect assignment priority"
        );

        check(
            decodedAssignment.dataSize ==
            sizeof(requestData),
            "Incorrect assignment data size"
        );

        check(
            std::memcmp(
                decodedAssignment.data,
                requestData,
                sizeof(requestData)
            ) == 0,
            "Incorrect assignment data"
        );

        std::cout
            << "       LB -> WR: ASSIGN\n";

        // STEP 5: Send a heartbeat.

        HeartbeatPayload heartbeat;

        sendChecked(
            network.worker.client,
            heartbeat
        );

        payload = receiveExpected(
            lbWorkerReceiver,
            network.worker.server,
            MessageType::HEARTBEAT,
            header
        );

        HeartbeatPayload decodedHeartbeat;

        check(
            PayloadCodec::deserializeHeartbeat(
                payload,
                header.length,
                decodedHeartbeat),
            "Heartbeat decoding failed"
        );

        consumeChecked(lbWorkerReceiver);

        std::cout
            << "       WR -> LB: HEARTBEAT\n";

        // STEP 6: Return the simulated processing result.
        // Echo the original bytes without business side effects.

        ResultPayload result;

        result.lbRequestId =
            decodedAssignment.lbRequestId;

        result.attemptId =
            decodedAssignment.attemptId;

        result.resultSize =
            decodedAssignment.dataSize;

        result.result =
            decodedAssignment.data;

        sendChecked(
            network.worker.client,
            result
        );

        consumeChecked(workerReceiver);

        payload = receiveExpected(
            lbWorkerReceiver,
            network.worker.server,
            MessageType::RESULT,
            header
        );

        ResultPayload decodedResult;

        check(
            PayloadCodec::deserializeResult(
                payload,
                header.length,
                decodedResult),
            "Worker result decoding failed"
        );

        check(
            decodedResult.lbRequestId == 1000,
            "Incorrect result request ID"
        );

        check(
            decodedResult.attemptId == 1,
            "Incorrect result attempt ID"
        );

        check(
            decodedResult.resultSize ==
            sizeof(requestData),
            "Incorrect worker result size"
        );

        check(
            std::memcmp(
                decodedResult.result,
                requestData,
                sizeof(requestData)
            ) == 0,
            "Incorrect worker result data"
        );

        std::cout
            << "       WR -> LB: RESULT\n";

        // STEP 7: Send the final response to the user.

        ResponsePayload response;

        response.clientRequestId =
            originalClientRequestId;

        response.status =
            ResponseStatus::SUCCESS;

        response.resultSize =
            decodedResult.resultSize;

        response.result =
            decodedResult.result;

        sendChecked(
            network.user.server,
            response
        );

        consumeChecked(lbWorkerReceiver);

        payload = receiveExpected(
            userReceiver,
            network.user.client,
            MessageType::RESPONSE,
            header
        );

        ResponsePayload decodedResponse;

        check(
            PayloadCodec::deserializeResponse(
                payload,
                header.length,
                decodedResponse),
            "Final response decoding failed"
        );

        check(
            decodedResponse.clientRequestId == 100,
            "Original client request ID was lost"
        );

        check(
            decodedResponse.status ==
            ResponseStatus::SUCCESS,
            "Incorrect final response status"
        );

        check(
            decodedResponse.resultSize ==
            sizeof(requestData),
            "Incorrect final result size"
        );

        check(
            std::memcmp(
                decodedResponse.result,
                requestData,
                sizeof(requestData)
            ) == 0,
            "Incorrect final response data"
        );

        consumeChecked(userReceiver);

        std::cout
            << "       LB -> UR: RESPONSE\n";

        int socketError = 0;

        check(
            userReceiver.receive(
                network.user.client,
                socketError
            ) == ReceiveStatus::WouldBlock,
            "Unexpected additional user response"
        );

        closeNetwork(network);

        std::cout
            << "[PASS] Complete UR-LB-WR flow test\n";
    }

    // Contains parameters for a large message sending thread.
    template<typename Payload>
    struct LargeSendTask
    {
        SOCKET socketHandle;
        const Payload* payload;

        std::size_t bytesSent = 0;

        int result = WSAEINVAL;
    };

    // Sends a large message while the main thread receives it.
    template<typename Payload>
    unsigned __stdcall largeSendThread(void* parameter)
    {
        LargeSendTask<Payload>* task =
            static_cast<LargeSendTask<Payload>*>(
                parameter
                );

        task->result = MessageIO::sendMessage(
            task->socketHandle,
            *task->payload,
            task->bytesSent,
            TEST_TIMEOUT_MS
        );

        return 0;
    }

    // Transfers a maximum-sized protocol message.
    template<typename Payload>
    const uint8_t* transferLargeMessage(
        SOCKET senderSocket,
        SOCKET receiverSocket,
        MessageReceiver& receiver,
        const Payload& message,
        MessageType type,
        MessageHeader& header
    )
    {
        LargeSendTask<Payload> task = {};

        task.socketHandle = senderSocket;
        task.payload = &message;

        HANDLE thread = startThread(
            largeSendThread<Payload>,
            &task
        );

        const uint8_t* payload =
            receiveExpected(
                receiver,
                receiverSocket,
                type,
                header
            );

        joinThread(thread);

        checkResult(
            task.result,
            "Maximum message sending failed"
        );

        check(
            task.bytesSent ==
            Constants::MAX_MESSAGE_SIZE,
            "Incorrect maximum sent message size"
        );

        check(
            header.length ==
            Constants::MAX_PAYLOAD_SIZE,
            "Incorrect maximum received payload size"
        );

        return payload;
    }

    // Tests maximum-sized ASSIGN and RESULT messages.
    void testMaximumMessageTransfer()
    {
        IntegrationNetwork network;

        createNetwork(network);

        MessageReceiver workerReceiver;
        MessageReceiver lbWorkerReceiver;

        static uint8_t requestData[
            Constants::MAX_REQUEST_DATA_SIZE
        ];

        static uint8_t resultData[
            Constants::MAX_RESULT_DATA_SIZE
        ];

        // Fill the buffers with deterministic binary data.
        for (uint32_t i = 0;
            i < Constants::MAX_REQUEST_DATA_SIZE;
            ++i)
        {
            requestData[i] =
                static_cast<uint8_t>(i % 256);
        }

        for (uint32_t i = 0;
            i < Constants::MAX_RESULT_DATA_SIZE;
            ++i)
        {
            resultData[i] =
                static_cast<uint8_t>(255 - i % 256);
        }

        AssignPayload assignment;

        assignment.lbRequestId = 5000;
        assignment.attemptId = 2;
        assignment.priority = Priority::MEDIUM;

        assignment.dataSize =
            Constants::MAX_REQUEST_DATA_SIZE;

        assignment.data = requestData;

        MessageHeader header = {};

        const uint8_t* payload =
            transferLargeMessage(
                network.worker.server,
                network.worker.client,
                workerReceiver,
                assignment,
                MessageType::ASSIGN,
                header
            );

        AssignPayload decodedAssignment;

        check(
            PayloadCodec::deserializeAssign(
                payload,
                header.length,
                decodedAssignment),
            "Maximum assignment decoding failed"
        );

        check(
            decodedAssignment.lbRequestId ==
            assignment.lbRequestId,
            "Incorrect maximum assignment ID"
        );

        check(
            decodedAssignment.attemptId ==
            assignment.attemptId,
            "Incorrect maximum assignment attempt"
        );

        check(
            decodedAssignment.priority ==
            assignment.priority,
            "Incorrect maximum assignment priority"
        );

        check(
            decodedAssignment.dataSize ==
            Constants::MAX_REQUEST_DATA_SIZE,
            "Maximum assignment size mismatch"
        );

        check(
            std::memcmp(
                decodedAssignment.data,
                requestData,
                sizeof(requestData)
            ) == 0,
            "Maximum assignment data mismatch"
        );

        consumeChecked(workerReceiver);

        // Send the maximum-sized result.

        ResultPayload result;

        result.lbRequestId = 5000;
        result.attemptId = 2;

        result.resultSize =
            Constants::MAX_RESULT_DATA_SIZE;

        result.result = resultData;

        payload = transferLargeMessage(
            network.worker.client,
            network.worker.server,
            lbWorkerReceiver,
            result,
            MessageType::RESULT,
            header
        );

        ResultPayload decodedResult;

        check(
            PayloadCodec::deserializeResult(
                payload,
                header.length,
                decodedResult),
            "Maximum result decoding failed"
        );

        check(
            decodedResult.lbRequestId ==
            result.lbRequestId,
            "Incorrect maximum result ID"
        );

        check(
            decodedResult.attemptId ==
            result.attemptId,
            "Incorrect maximum result attempt"
        );

        check(
            decodedResult.resultSize ==
            Constants::MAX_RESULT_DATA_SIZE,
            "Maximum result size mismatch"
        );

        check(
            std::memcmp(
                decodedResult.result,
                resultData,
                sizeof(resultData)
            ) == 0,
            "Maximum result data mismatch"
        );

        consumeChecked(lbWorkerReceiver);

        closeNetwork(network);

        std::cout
            << "       ASSIGN size: "
            << Constants::MAX_MESSAGE_SIZE
            << " bytes\n";

        std::cout
            << "       RESULT size: "
            << Constants::MAX_MESSAGE_SIZE
            << " bytes\n";

        std::cout
            << "[PASS] Maximum message transfer test\n";
    }

    // Contains parameters for a concurrent worker sender.
    struct ConcurrentSendTask
    {
        SOCKET socketHandle;

        CRITICAL_SECTION* sendLock;

        HANDLE startEvent;

        bool sendHeartbeat;

        int result = WSAEINVAL;
    };

    // Sends heartbeat or result messages through a shared socket.
    unsigned __stdcall concurrentSendThread(
        void* parameter
    )
    {
        ConcurrentSendTask* task =
            static_cast<ConcurrentSendTask*>(
                parameter
                );

        const DWORD waitResult =
            WaitForSingleObject(
                task->startEvent,
                TEST_TIMEOUT_MS
            );

        if (waitResult != WAIT_OBJECT_0)
        {
            task->result = WSAETIMEDOUT;

            return 0;
        }

        task->result = 0;

        const uint8_t resultData[] =
        {
            0, 10, 20, 30, 255
        };

        for (uint32_t i = 0;
            i < CONCURRENT_MESSAGE_COUNT;
            ++i)
        {
            std::size_t bytesSent = 0;
            std::size_t expectedSize = 0;

            // Protect the entire protocol message.
            EnterCriticalSection(task->sendLock);

            if (task->sendHeartbeat)
            {
                HeartbeatPayload heartbeat;

                expectedSize =
                    Constants::HEADER_SIZE;

                task->result =
                    MessageIO::sendMessage(
                        task->socketHandle,
                        heartbeat,
                        bytesSent,
                        TEST_TIMEOUT_MS
                    );
            }
            else
            {
                ResultPayload result;

                result.lbRequestId =
                    1000 + i;

                result.attemptId = 1;

                result.resultSize =
                    static_cast<uint32_t>(
                        sizeof(resultData)
                        );

                result.result = resultData;

                expectedSize =
                    Constants::HEADER_SIZE +
                    Constants::RESULT_FIXED_SIZE +
                    sizeof(resultData);

                task->result =
                    MessageIO::sendMessage(
                        task->socketHandle,
                        result,
                        bytesSent,
                        TEST_TIMEOUT_MS
                    );
            }

            LeaveCriticalSection(task->sendLock);

            if (task->result != 0)
            {
                return 0;
            }

            if (bytesSent != expectedSize)
            {
                task->result = WSAEINVAL;

                return 0;
            }

            // Give the other sending thread a chance to run.
            Sleep(0);
        }

        return 0;
    }

    // Receives the next message of any supported type.
    const uint8_t* receiveNext(
        MessageReceiver& receiver,
        SOCKET socketHandle,
        MessageHeader& header
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

            if (status == ReceiveStatus::MessageReady)
            {
                const uint8_t* payload = nullptr;

                check(
                    receiver.getMessage(header, payload),
                    "Concurrent message retrieval failed"
                );

                return payload;
            }

            if (status == ReceiveStatus::SocketError)
            {
                checkResult(
                    socketError,
                    "Concurrent reception failed"
                );
            }

            check(
                status == ReceiveStatus::WouldBlock,
                "Invalid concurrent protocol message"
            );

            const ULONGLONG elapsed =
                GetTickCount64() - startTime;

            check(
                elapsed < TEST_TIMEOUT_MS,
                "Concurrent message reception timed out"
            );

            const uint32_t remaining =
                TEST_TIMEOUT_MS -
                static_cast<uint32_t>(elapsed);

            waitReadable(socketHandle, remaining);
        }
    }

    // Tests synchronized heartbeat and result sending.
    void testConcurrentWorkerSending()
    {
        IntegrationNetwork network;

        createNetwork(network);

        MessageReceiver receiver;

        CRITICAL_SECTION csSend;

        InitializeCriticalSection(&csSend);

        HANDLE startEvent = CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

        if (startEvent == nullptr)
        {
            std::cerr
                << "[FAIL] Start event creation failed\n";

            std::abort();
        }

        ConcurrentSendTask heartbeatTask = {};

        heartbeatTask.socketHandle =
            network.worker.client;

        heartbeatTask.sendLock = &csSend;

        heartbeatTask.startEvent = startEvent;

        heartbeatTask.sendHeartbeat = true;

        ConcurrentSendTask resultTask = {};

        resultTask.socketHandle =
            network.worker.client;

        resultTask.sendLock = &csSend;

        resultTask.startEvent = startEvent;

        resultTask.sendHeartbeat = false;

        // Simulate the THB and TP worker threads.
        HANDLE heartbeatThread = startThread(
            concurrentSendThread,
            &heartbeatTask
        );

        HANDLE processingThread = startThread(
            concurrentSendThread,
            &resultTask
        );

        check(
            SetEvent(startEvent) != 0,
            "Could not start concurrent sending"
        );

        uint32_t heartbeatCount = 0;
        uint32_t resultCount = 0;

        bool receivedResults[
            CONCURRENT_MESSAGE_COUNT
        ] = {};

            const uint8_t expectedResult[] =
            {
                0, 10, 20, 30, 255
            };

            // Receive all messages on the load balancer side.
            for (uint32_t i = 0;
                i < CONCURRENT_MESSAGE_COUNT * 2;
                ++i)
            {
                MessageHeader header = {};

                const uint8_t* payload =
                    receiveNext(
                        receiver,
                        network.worker.server,
                        header
                    );

                if (header.type == MessageType::HEARTBEAT)
                {
                    HeartbeatPayload heartbeat;

                    check(
                        PayloadCodec::deserializeHeartbeat(
                            payload,
                            header.length,
                            heartbeat),
                        "Corrupted heartbeat message"
                    );

                    ++heartbeatCount;
                }
                else if (header.type == MessageType::RESULT)
                {
                    ResultPayload result;

                    check(
                        PayloadCodec::deserializeResult(
                            payload,
                            header.length,
                            result),
                        "Corrupted result message"
                    );

                    check(
                        result.lbRequestId >= 1000 &&
                        result.lbRequestId <
                        1000 + CONCURRENT_MESSAGE_COUNT,
                        "Unexpected concurrent request ID"
                    );

                    const uint32_t index =
                        static_cast<uint32_t>(
                            result.lbRequestId - 1000
                            );

                    check(
                        !receivedResults[index],
                        "Duplicate concurrent result detected"
                    );

                    check(
                        index == resultCount,
                        "Result message order was not preserved"
                    );

                    check(
                        result.attemptId == 1,
                        "Incorrect concurrent attempt ID"
                    );

                    check(
                        result.resultSize ==
                        sizeof(expectedResult),
                        "Incorrect concurrent result size"
                    );

                    check(
                        std::memcmp(
                            result.result,
                            expectedResult,
                            sizeof(expectedResult)
                        ) == 0,
                        "Concurrent result data mismatch"
                    );

                    receivedResults[index] = true;

                    ++resultCount;
                }
                else
                {
                    check(
                        false,
                        "Unexpected concurrent message type"
                    );
                }

                consumeChecked(receiver);
            }

            joinThread(heartbeatThread);
            joinThread(processingThread);

            checkResult(
                heartbeatTask.result,
                "Heartbeat sending thread failed"
            );

            checkResult(
                resultTask.result,
                "Result sending thread failed"
            );

            check(
                heartbeatCount ==
                CONCURRENT_MESSAGE_COUNT,
                "Incorrect received heartbeat count"
            );

            check(
                resultCount ==
                CONCURRENT_MESSAGE_COUNT,
                "Incorrect received result count"
            );

            for (uint32_t i = 0;
                i < CONCURRENT_MESSAGE_COUNT;
                ++i)
            {
                check(
                    receivedResults[i],
                    "Concurrent result was lost"
                );
            }

            int socketError = 0;

            check(
                receiver.receive(
                    network.worker.server,
                    socketError
                ) == ReceiveStatus::WouldBlock,
                "Unexpected additional concurrent message"
            );

            check(
                CloseHandle(startEvent) != 0,
                "Start event cleanup failed"
            );

            DeleteCriticalSection(&csSend);

            closeNetwork(network);

            std::cout
                << "       Heartbeats received: "
                << heartbeatCount << "\n";

            std::cout
                << "       Results received: "
                << resultCount << "\n";

            std::cout
                << "[PASS] Concurrent worker sending test\n";
    }
}

// Runs all network integration tests.
void runNetworkIntegrationTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for integration tests failed"
    );

    testCompleteCommunicationFlow();
    testMaximumMessageTransfer();
    testConcurrentWorkerSending();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after integration tests failed"
    );

    std::cout
        << "All network integration tests passed.\n";
}