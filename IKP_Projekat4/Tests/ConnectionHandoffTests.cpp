#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <new>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "TcpListener.h"
#include "TcpClient.h"
#include "ConnectionContext.h"
#include "ConnectionHandoff.h"
#include "MessageIO.h"
#include "SendUtils.h"
#include "Protocol.h"
#include "PayloadCodec.h"
#include "Constants.h"
#include "TestSuites.h"

namespace
{
    constexpr uint32_t TEST_TIMEOUT_MS = 5000;

    constexpr uint64_t TRANSFER_COUNT = 1000;

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

    // Allocates a connection context.
    ConnectionContext* createContext(
        SOCKET& socketHandle
    )
    {
        ConnectionContext* connection =
            new (std::nothrow)
            ConnectionContext(socketHandle);

        if (connection == nullptr)
        {
            std::cerr
                << "[FAIL] Connection allocation failed\n";

            std::abort();
        }

        return connection;
    }

    // Creates a context without a real network socket.
    // Used for isolated FIFO tests.
    ConnectionContext* createTestContext(uint64_t id)
    {
        SOCKET socketHandle = INVALID_SOCKET;

        ConnectionContext* connection =
            createContext(socketHandle);

        connection->role = ConnectionRole::Worker;

        connection->workerId = id;

        connection->sessionId = id;

        return connection;
    }

    // Starts a test thread.
    HANDLE startThread(
        unsigned(__stdcall* entryPoint)(void*),
        void* parameter
    )
    {
        const uintptr_t handle =
            _beginthreadex(
                nullptr,
                0,
                entryPoint,
                parameter,
                0,
                nullptr
            );

        if (handle == 0)
        {
            std::cerr
                << "[FAIL] Test thread creation failed\n";

            std::abort();
        }

        return reinterpret_cast<HANDLE>(handle);
    }

    // Waits for a test thread and releases its handle.
    void joinThread(HANDLE thread)
    {
        const DWORD result =
            WaitForSingleObject(thread, 10000);

        check(
            result == WAIT_OBJECT_0,
            "Test thread did not finish"
        );

        check(
            CloseHandle(thread) != 0,
            "Thread handle cleanup failed"
        );
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
            static_cast<long>(
                (timeoutMs % 1000) * 1000
                );

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

    // Receives a complete message or a terminal status.
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
                    "Socket reception failed"
                );
            }

            if (status != ReceiveStatus::WouldBlock)
            {
                return status;
            }

            const ULONGLONG elapsed =
                GetTickCount64() - startTime;

            check(
                elapsed < TEST_TIMEOUT_MS,
                "Message reception timed out"
            );

            waitReadable(
                socketHandle,
                TEST_TIMEOUT_MS -
                static_cast<uint32_t>(elapsed)
            );
        }
    }

    // Creates a real TCP connection.
    ConnectionContext* createNetworkContext(
        TcpListener& listener,
        TcpClient& client
    )
    {
        checkResult(
            listener.start("127.0.0.1", 0),
            "Test listener startup failed"
        );

        checkResult(
            client.connectTo(
                "127.0.0.1",
                listener.getPort()),
            "Test client connection failed"
        );

        waitReadable(
            listener.getSocket(),
            TEST_TIMEOUT_MS
        );

        SOCKET acceptedSocket = INVALID_SOCKET;

        checkResult(
            SocketUtils::acceptSocket(
                listener.getSocket(),
                acceptedSocket),
            "Connection acceptance failed"
        );

        checkResult(
            SocketUtils::setNonBlocking(
                acceptedSocket, true),
            "Accepted socket configuration failed"
        );

        const SOCKET originalSocket =
            acceptedSocket;

        ConnectionContext* connection =
            createContext(acceptedSocket);

        check(
            acceptedSocket == INVALID_SOCKET,
            "Original socket ownership was not released"
        );

        check(
            connection->getSocket() == originalSocket,
            "Context contains an incorrect socket"
        );

        check(
            connection->hasSocket(),
            "Context does not own a socket"
        );

        return connection;
    }

    // Sends raw protocol bytes.
    void sendRaw(
        SOCKET socketHandle,
        const uint8_t* data,
        std::size_t size
    )
    {
        std::size_t bytesSent = 0;

        checkResult(
            SendUtils::sendAll(
                socketHandle,
                data,
                size,
                bytesSent,
                TEST_TIMEOUT_MS),
            "Raw message sending failed"
        );

        check(
            bytesSent == size,
            "Raw message was not sent completely"
        );
    }

    // Tests an empty handoff queue.
    void testInitialState()
    {
        ConnectionHandoff handoff;

        check(
            handoff.isEmpty(),
            "New handoff queue is not empty"
        );

        check(
            handoff.size() == 0,
            "Initial handoff size is invalid"
        );

        check(
            !handoff.isSealed(),
            "New handoff queue is sealed"
        );

        check(
            handoff.tryPop() == nullptr,
            "Empty queue returned a connection"
        );

        ConnectionContext* empty = nullptr;

        check(
            !handoff.tryPush(empty),
            "Null connection was accepted"
        );

        std::cout
            << "[PASS] Initial handoff state test\n";
    }

    // Tests FIFO ordering and ownership transfer.
    void testFifoOwnership()
    {
        ConnectionHandoff handoff;

        for (uint64_t id = 1; id <= 3; ++id)
        {
            ConnectionContext* connection =
                createTestContext(id);

            check(
                handoff.tryPush(connection),
                "Connection transfer failed"
            );

            check(
                connection == nullptr,
                "Producer retained connection ownership"
            );
        }

        check(
            handoff.size() == 3,
            "Incorrect handoff queue size"
        );

        for (uint64_t id = 1; id <= 3; ++id)
        {
            ConnectionContext* connection =
                handoff.tryPop();

            if (connection == nullptr)
            {
                std::cerr
                    << "[FAIL] Missing queued connection\n";

                std::abort();
            }

            check(
                connection->workerId == id,
                "FIFO connection order was violated"
            );

            check(
                connection->sessionId == id,
                "Connection session was not preserved"
            );

            delete connection;
        }

        check(
            handoff.isEmpty(),
            "Handoff queue did not become empty"
        );

        std::cout
            << "[PASS] FIFO ownership transfer test\n";
    }

    // Tests full capacity and circular queue wraparound.
    void testCapacityAndWraparound()
    {
        ConnectionHandoff handoff;

        const std::size_t capacity =
            ConnectionHandoff::CAPACITY;

        // Fill every queue slot.
        for (std::size_t i = 0; i < capacity; ++i)
        {
            ConnectionContext* connection =
                createTestContext(i + 1);

            check(
                handoff.tryPush(connection),
                "Queue rejected a valid connection"
            );
        }

        check(
            handoff.size() == capacity,
            "Incorrect full queue size"
        );

        // A failed transfer must preserve ownership.
        ConnectionContext* extra =
            createTestContext(capacity + 1);

        ConnectionContext* original = extra;

        check(
            !handoff.tryPush(extra),
            "Full queue accepted another connection"
        );

        check(
            extra == original,
            "Failed transfer lost connection ownership"
        );

        delete extra;

        // Remove half of the original entries.
        const std::size_t half = capacity / 2;

        for (std::size_t i = 0; i < half; ++i)
        {
            ConnectionContext* connection =
                handoff.tryPop();

            check(
                connection != nullptr &&
                connection->workerId == i + 1,
                "Incorrect connection removed"
            );

            delete connection;
        }

        // Reuse the released circular queue slots.
        for (std::size_t i = 0; i < half; ++i)
        {
            ConnectionContext* connection =
                createTestContext(capacity + i + 1);

            check(
                handoff.tryPush(connection),
                "Circular queue slot reuse failed"
            );
        }

        check(
            handoff.size() == capacity,
            "Incorrect queue size after wraparound"
        );

        // Verify the remaining original and new entries.
        for (std::size_t i = half;
            i < capacity + half;
            ++i)
        {
            ConnectionContext* connection =
                handoff.tryPop();

            check(
                connection != nullptr &&
                connection->workerId == i + 1,
                "Circular queue order was violated"
            );

            delete connection;
        }

        check(
            handoff.isEmpty(),
            "Circular queue was not emptied"
        );

        std::cout
            << "[PASS] Handoff capacity and wraparound test\n";
    }

    // Tests sealing while connections remain queued.
    void testSeal()
    {
        ConnectionHandoff handoff;

        ConnectionContext* first =
            createTestContext(1);

        check(
            handoff.tryPush(first),
            "Initial connection transfer failed"
        );

        handoff.seal();
        handoff.seal();

        check(
            handoff.isSealed(),
            "Handoff queue was not sealed"
        );

        ConnectionContext* second =
            createTestContext(2);

        check(
            !handoff.tryPush(second),
            "Sealed queue accepted another connection"
        );

        check(
            second != nullptr,
            "Rejected connection ownership was lost"
        );

        delete second;

        ConnectionContext* received =
            handoff.tryPop();

        check(
            received != nullptr &&
            received->workerId == 1,
            "Sealing discarded a queued connection"
        );

        delete received;

        check(
            handoff.isEmpty(),
            "Sealed queue was not emptied"
        );

        std::cout
            << "[PASS] Handoff seal and drain test\n";
    }

    // Consumes connections in a separate thread.
    unsigned __stdcall consumerThread(void* parameter)
    {
        ConnectionHandoff* handoff =
            static_cast<ConnectionHandoff*>(parameter);

        ULONGLONG lastProgress =
            GetTickCount64();

        for (uint64_t expectedId = 1;
            expectedId <= TRANSFER_COUNT;
            ++expectedId)
        {
            ConnectionContext* connection = nullptr;

            while ((connection = handoff->tryPop())
                == nullptr)
            {
                check(
                    GetTickCount64() - lastProgress <
                    TEST_TIMEOUT_MS,
                    "Concurrent consumer timed out"
                );

                Sleep(1);
            }

            lastProgress = GetTickCount64();

            check(
                connection->workerId == expectedId,
                "Concurrent transfer order was violated"
            );

            check(
                connection->sessionId == expectedId,
                "Concurrent session data was corrupted"
            );

            delete connection;
        }

        return 0;
    }

    // Tests 1000 transfers between two concurrent threads.
    void testConcurrentTransfers()
    {
        ConnectionHandoff handoff;

        HANDLE consumer = startThread(
            consumerThread,
            &handoff
        );

        // The main thread simulates the producer.
        for (uint64_t id = 1;
            id <= TRANSFER_COUNT;
            ++id)
        {
            ConnectionContext* connection =
                createTestContext(id);

            const ULONGLONG startTime =
                GetTickCount64();

            while (!handoff.tryPush(connection))
            {
                check(
                    GetTickCount64() - startTime <
                    TEST_TIMEOUT_MS,
                    "Concurrent producer timed out"
                );

                Sleep(1);
            }

            check(
                connection == nullptr,
                "Producer retained a transferred connection"
            );
        }

        joinThread(consumer);

        check(
            handoff.isEmpty(),
            "Concurrent handoff queue is not empty"
        );

        std::cout
            << "       Transferred connections: "
            << TRANSFER_COUNT << "\n";

        std::cout
            << "[PASS] Concurrent connection transfers test\n";
    }

    // Simulates TWR receiving a handed-off connection.
    unsigned __stdcall workerReceiverThread(
        void* parameter
    )
    {
        ConnectionHandoff* handoff =
            static_cast<ConnectionHandoff*>(parameter);

        ConnectionContext* connection =
            handoff->tryPop();

        if (connection == nullptr)
        {
            std::cerr
                << "[FAIL] Worker connection is missing\n";

            std::abort();
        }

        check(
            connection->role == ConnectionRole::Worker,
            "Incorrect transferred connection role"
        );

        check(
            connection->workerId == 42,
            "Worker identifier was lost during transfer"
        );

        check(
            connection->sessionId == 7,
            "Worker session was lost during transfer"
        );

        check(
            connection->hasSocket(),
            "Transferred connection lost its socket"
        );

        // Continue receiving with the original receiver.
        check(
            waitForReceive(
                connection->receiver,
                connection->getSocket()
            ) == ReceiveStatus::MessageReady,
            "Transferred receiver did not complete the message"
        );

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        check(
            connection->receiver.getMessage(
                header, payload),
            "Transferred message retrieval failed"
        );

        check(
            header.type == MessageType::HEARTBEAT,
            "Incorrect message after connection transfer"
        );

        HeartbeatPayload heartbeat;

        check(
            PayloadCodec::deserializeHeartbeat(
                payload,
                header.length,
                heartbeat),
            "Transferred heartbeat is corrupted"
        );

        check(
            connection->receiver.consumeMessage(),
            "Heartbeat consumption failed"
        );

        checkResult(
            connection->close(),
            "Transferred socket cleanup failed"
        );

        delete connection;

        return 0;
    }

    // Tests transfer of a partially received TCP message.
    void testPartialMessageHandoff()
    {
        TcpListener listener;
        TcpClient client;

        ConnectionHandoff handoff;

        ConnectionContext* connection =
            createNetworkContext(listener, client);

        // STEP 1: Send a worker registration.
        RegisterWrPayload registration;

        registration.workerId = 42;

        std::size_t bytesSent = 0;

        checkResult(
            MessageIO::sendMessage(
                client.getSocket(),
                registration,
                bytesSent),
            "Worker registration sending failed"
        );

        // STEP 2: Simulate TUR receiving registration.
        check(
            waitForReceive(
                connection->receiver,
                connection->getSocket()
            ) == ReceiveStatus::MessageReady,
            "Worker registration was not received"
        );

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        check(
            connection->receiver.getMessage(
                header, payload),
            "Registration retrieval failed"
        );

        check(
            header.type == MessageType::REGISTER_WR,
            "Incorrect registration message type"
        );

        RegisterWrPayload decodedRegistration;

        check(
            PayloadCodec::deserializeRegisterWr(
                payload,
                header.length,
                decodedRegistration),
            "Worker registration decoding failed"
        );

        connection->role = ConnectionRole::Worker;

        connection->workerId =
            decodedRegistration.workerId;

        connection->sessionId = 7;

        check(
            connection->receiver.consumeMessage(),
            "Registration consumption failed"
        );

        // STEP 3: Build a heartbeat protocol header.
        MessageHeader heartbeatHeader = {};

        heartbeatHeader.length = 0;

        heartbeatHeader.type =
            MessageType::HEARTBEAT;

        heartbeatHeader.version =
            Constants::PROTOCOL_VERSION;

        uint8_t heartbeatBytes[
            Constants::HEADER_SIZE
        ] = {};

            check(
                Protocol::serializeHeader(
                    heartbeatHeader,
                    heartbeatBytes,
                    sizeof(heartbeatBytes)),
                "Heartbeat header serialization failed"
            );

            // Send only the first three bytes.
            constexpr std::size_t FIRST_PART_SIZE = 3;

            sendRaw(
                client.getSocket(),
                heartbeatBytes,
                FIRST_PART_SIZE
            );

            waitReadable(
                connection->getSocket(),
                TEST_TIMEOUT_MS
            );

            int socketError = 0;

            // TUR receives an incomplete heartbeat.
            check(
                connection->receiver.receive(
                    connection->getSocket(),
                    socketError
                ) == ReceiveStatus::WouldBlock,
                "Partial heartbeat was incorrectly completed"
            );

            check(
                !connection->receiver.hasMessage(),
                "Incomplete heartbeat became available"
            );

            // STEP 4: Transfer the original connection context.
            connection->needsDrain = true;

            check(
                handoff.tryPush(connection),
                "Worker connection handoff failed"
            );

            check(
                connection == nullptr,
                "TUR retained transferred connection ownership"
            );

            // STEP 5: Simulate TWR taking ownership.
            HANDLE receiverThread = startThread(
                workerReceiverThread,
                &handoff
            );

            // Complete the previously fragmented heartbeat.
            sendRaw(
                client.getSocket(),
                heartbeatBytes + FIRST_PART_SIZE,
                sizeof(heartbeatBytes) - FIRST_PART_SIZE
            );

            joinThread(receiverThread);

            check(
                handoff.isEmpty(),
                "Transferred connection remained queued"
            );

            // Verify that TWR closed the transferred socket.
            MessageReceiver clientReceiver;

            check(
                waitForReceive(
                    clientReceiver,
                    client.getSocket()
                ) == ReceiveStatus::ConnectionClosed,
                "Transferred socket remained open"
            );

            checkResult(
                client.disconnect(),
                "Test client cleanup failed"
            );

            checkResult(
                listener.stop(),
                "Test listener cleanup failed"
            );

            std::cout
                << "       TUR received 3 initial bytes\n"
                << "       TWR completed the heartbeat message\n";

            std::cout
                << "[PASS] Partial message connection handoff test\n";
    }

    // Tests cleanup of sockets still owned by the handoff queue.
    void testPendingConnectionCleanup()
    {
        TcpListener listener;
        TcpClient client;

        {
            ConnectionHandoff handoff;

            ConnectionContext* connection =
                createNetworkContext(
                    listener,
                    client
                );

            check(
                handoff.tryPush(connection),
                "Pending connection transfer failed"
            );

            check(
                handoff.size() == 1,
                "Pending connection is missing"
            );

            // The queue destructor owns the connection
            // and must release its socket.
        }

        MessageReceiver receiver;

        check(
            waitForReceive(
                receiver,
                client.getSocket()
            ) == ReceiveStatus::ConnectionClosed,
            "Pending socket was not released"
        );

        checkResult(
            client.disconnect(),
            "Pending client cleanup failed"
        );

        checkResult(
            listener.stop(),
            "Pending listener cleanup failed"
        );

        std::cout
            << "[PASS] Pending connection cleanup test\n";
    }
}

// Runs all connection handoff tests.
void runConnectionHandoffTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for handoff tests failed"
    );

    testInitialState();
    testFifoOwnership();
    testCapacityAndWraparound();
    testSeal();
    testConcurrentTransfers();
    testPartialMessageHandoff();
    testPendingConnectionCleanup();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after handoff tests failed"
    );

    std::cout
        << "All connection handoff tests passed.\n";
}