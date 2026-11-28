#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstddef>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "TcpListener.h"
#include "TcpClient.h"
#include "MessageIO.h"
#include "PayloadCodec.h"
#include "Constants.h"
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

    // Waits for socket readiness.
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

    // Starts a temporary test server.
    void startListener(TcpListener& listener)
    {
        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Test listener startup failed"
        );

        check(
            listener.getPort() != 0,
            "Invalid test listener port"
        );
    }

    // Establishes a worker connection.
    void connectWorker(
        TcpListener& listener,
        TcpClient& worker,
        SOCKET& serverSocket
    )
    {
        checkResult(
            worker.connectTo(
                LOCAL_ADDRESS,
                listener.getPort()),
            "Worker connection failed"
        );

        check(
            worker.isConnected(),
            "Worker connection state is invalid"
        );

        waitReadable(
            listener.getSocket(),
            TEST_TIMEOUT_MS
        );

        checkResult(
            SocketUtils::acceptSocket(
                listener.getSocket(),
                serverSocket),
            "Worker acceptance failed"
        );

        checkResult(
            SocketUtils::setNonBlocking(
                serverSocket,
                true),
            "Worker server socket configuration failed"
        );

        check(
            serverSocket != INVALID_SOCKET,
            "Invalid worker server socket"
        );
    }

    // Sends a worker registration message.
    void sendRegistration(
        TcpClient& worker,
        uint64_t workerId
    )
    {
        RegisterWrPayload registration;

        registration.workerId = workerId;

        std::size_t bytesSent = 0;

        checkResult(
            MessageIO::sendMessage(
                worker.getSocket(),
                registration,
                bytesSent,
                TEST_TIMEOUT_MS),
            "Worker registration sending failed"
        );

        check(
            bytesSent ==
            Constants::HEADER_SIZE +
            sizeof(uint64_t),
            "Incorrect worker registration message size"
        );
    }

    // Waits for a complete registration message.
    void waitForMessage(
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

            if (status == ReceiveStatus::MessageReady)
            {
                return;
            }

            if (status == ReceiveStatus::SocketError)
            {
                checkResult(
                    socketError,
                    "Worker registration reception failed"
                );
            }

            check(
                status == ReceiveStatus::WouldBlock,
                "Unexpected worker receive status"
            );

            const ULONGLONG elapsed =
                GetTickCount64() - startTime;

            check(
                elapsed < TEST_TIMEOUT_MS,
                "Worker registration reception timed out"
            );

            const uint32_t remaining =
                TEST_TIMEOUT_MS -
                static_cast<uint32_t>(elapsed);

            waitReadable(
                socketHandle,
                remaining
            );
        }
    }

    // Receives and validates a worker registration.
    uint64_t receiveRegistration(
        MessageReceiver& receiver,
        SOCKET serverSocket
    )
    {
        waitForMessage(
            receiver,
            serverSocket
        );

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        check(
            receiver.getMessage(header, payload),
            "Worker registration retrieval failed"
        );

        check(
            header.type == MessageType::REGISTER_WR,
            "Unexpected worker registration type"
        );

        check(
            header.version ==
            Constants::PROTOCOL_VERSION,
            "Incorrect worker protocol version"
        );

        check(
            header.length == sizeof(uint64_t),
            "Incorrect worker registration payload size"
        );

        check(
            payload != nullptr,
            "Worker registration payload is null"
        );

        RegisterWrPayload registration;

        check(
            PayloadCodec::deserializeRegisterWr(
                payload,
                header.length,
                registration),
            "Worker registration decoding failed"
        );

        // Save the identifier before consuming the message.
        const uint64_t workerId =
            registration.workerId;

        check(
            receiver.consumeMessage(),
            "Worker registration consumption failed"
        );

        check(
            !receiver.hasMessage(),
            "Consumed worker message is still available"
        );

        return workerId;
    }

    // Closes a worker connection.
    void closeWorker(
        TcpClient& worker,
        SOCKET& serverSocket
    )
    {
        checkResult(
            worker.disconnect(),
            "Worker disconnection failed"
        );

        checkResult(
            SocketUtils::closeSocket(serverSocket),
            "Worker server socket cleanup failed"
        );

        check(
            !worker.isConnected(),
            "Worker remained connected"
        );

        check(
            serverSocket == INVALID_SOCKET,
            "Worker server socket was not invalidated"
        );
    }

    // Tests the registration of a single worker.
    void testSingleWorkerRegistration()
    {
        TcpListener listener;
        TcpClient worker;

        SOCKET serverSocket = INVALID_SOCKET;

        startListener(listener);

        connectWorker(
            listener,
            worker,
            serverSocket
        );

        const uint64_t expectedId = 42;

        sendRegistration(
            worker,
            expectedId
        );

        MessageReceiver receiver;

        const uint64_t receivedId =
            receiveRegistration(
                receiver,
                serverSocket
            );

        check(
            receivedId == expectedId,
            "Incorrect registered worker ID"
        );

        int socketError = 0;

        check(
            receiver.receive(
                serverSocket,
                socketError
            ) == ReceiveStatus::WouldBlock,
            "Unexpected additional registration message"
        );

        closeWorker(
            worker,
            serverSocket
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] Single worker registration test\n";
    }

    // Tests independent registration of multiple workers.
    void testMultipleWorkerRegistrations()
    {
        constexpr int WORKER_COUNT = 3;

        TcpListener listener;

        TcpClient workers[WORKER_COUNT];

        SOCKET serverSockets[WORKER_COUNT] =
        {
            INVALID_SOCKET,
            INVALID_SOCKET,
            INVALID_SOCKET
        };

        MessageReceiver receivers[WORKER_COUNT];

        startListener(listener);

        // Establish three separate TCP connections.
        for (int i = 0; i < WORKER_COUNT; ++i)
        {
            connectWorker(
                listener,
                workers[i],
                serverSockets[i]
            );

            for (int j = 0; j < i; ++j)
            {
                check(
                    serverSockets[i] != serverSockets[j],
                    "Workers share the same server socket"
                );
            }
        }

        // Send all registrations before receiving them.
        for (int i = 0; i < WORKER_COUNT; ++i)
        {
            sendRegistration(
                workers[i],
                static_cast<uint64_t>(i + 1)
            );
        }

        // Receive registrations in reverse order.
        for (int i = WORKER_COUNT - 1; i >= 0; --i)
        {
            const uint64_t receivedId =
                receiveRegistration(
                    receivers[i],
                    serverSockets[i]
                );

            check(
                receivedId ==
                static_cast<uint64_t>(i + 1),
                "Registration associated with the wrong socket"
            );
        }

        // Release all worker connections.
        for (int i = 0; i < WORKER_COUNT; ++i)
        {
            closeWorker(
                workers[i],
                serverSockets[i]
            );
        }

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "       Registered worker IDs: 1, 2, 3\n";

        std::cout
            << "[PASS] Multiple worker registrations test\n";
    }

    // Tests the maximum supported 64-bit identifier.
    void testMaximumWorkerId()
    {
        TcpListener listener;
        TcpClient worker;

        SOCKET serverSocket = INVALID_SOCKET;

        startListener(listener);

        connectWorker(
            listener,
            worker,
            serverSocket
        );

        const uint64_t maximumId = UINT64_MAX;

        sendRegistration(
            worker,
            maximumId
        );

        MessageReceiver receiver;

        const uint64_t receivedId =
            receiveRegistration(
                receiver,
                serverSocket
            );

        check(
            receivedId == maximumId,
            "Maximum worker ID was corrupted"
        );

        closeWorker(
            worker,
            serverSocket
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] Maximum 64-bit worker ID test\n";
    }
}

// Runs all worker registration tests.
void runWorkerRegistrationTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for worker tests failed"
    );

    testSingleWorkerRegistration();
    testMultipleWorkerRegistrations();
    testMaximumWorkerId();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after worker tests failed"
    );

    std::cout
        << "All worker registration tests passed.\n";
}