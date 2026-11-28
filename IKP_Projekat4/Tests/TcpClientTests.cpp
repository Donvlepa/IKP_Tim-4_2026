#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <type_traits>

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

    // Verifies exclusive socket ownership.
    static_assert(
        !std::is_copy_constructible<TcpClient>::value,
        "TcpClient must not be copy constructible"
        );

    static_assert(
        !std::is_copy_assignable<TcpClient>::value,
        "TcpClient must not be copy assignable"
        );

    static_assert(
        !std::is_move_constructible<TcpClient>::value,
        "TcpClient must not be move constructible"
        );

    static_assert(
        !std::is_move_assignable<TcpClient>::value,
        "TcpClient must not be move assignable"
        );

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

    // Verifies that a client is disconnected.
    void checkDisconnected(const TcpClient& client)
    {
        check(
            !client.isConnected(),
            "Client is still connected"
        );

        check(
            client.getSocket() == INVALID_SOCKET,
            "Disconnected client contains a socket"
        );
    }

    // Starts a local test listener.
    void startListener(TcpListener& listener)
    {
        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Test listener startup failed"
        );

        check(
            listener.getPort() != 0,
            "Test listener port is invalid"
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

    // Accepts a connected test client.
    void acceptClient(
        TcpListener& listener,
        SOCKET& acceptedSocket
    )
    {
        waitReadable(
            listener.getSocket(),
            TEST_TIMEOUT_MS
        );

        checkResult(
            SocketUtils::acceptSocket(
                listener.getSocket(),
                acceptedSocket),
            "Client acceptance failed"
        );

        checkResult(
            SocketUtils::setNonBlocking(
                acceptedSocket, true),
            "Accepted socket configuration failed"
        );
    }

    // Connects a client to the local test listener.
    void connectClient(
        TcpClient& client,
        TcpListener& listener,
        SOCKET& acceptedSocket
    )
    {
        checkResult(
            client.connectTo(
                LOCAL_ADDRESS,
                listener.getPort()),
            "Client connection failed"
        );

        acceptClient(listener, acceptedSocket);
    }

    // Receives a message or a terminal receive status.
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
                    "Message reception failed"
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

            const uint32_t remaining =
                TEST_TIMEOUT_MS -
                static_cast<uint32_t>(elapsed);

            waitReadable(socketHandle, remaining);
        }
    }

    // Retrieves a complete message of the expected type.
    const uint8_t* receiveExpected(
        MessageReceiver& receiver,
        SOCKET socketHandle,
        MessageType expectedType,
        MessageHeader& header
    )
    {
        check(
            waitForReceive(
                receiver,
                socketHandle
            ) == ReceiveStatus::MessageReady,
            "Expected message was not received"
        );

        const uint8_t* payload = nullptr;

        check(
            receiver.getMessage(header, payload),
            "Message retrieval failed"
        );

        check(
            header.type == expectedType,
            "Incorrect message type"
        );

        check(
            header.version ==
            Constants::PROTOCOL_VERSION,
            "Incorrect protocol version"
        );

        return payload;
    }

    // Closes a connected test client and its server socket.
    void closeConnection(
        TcpClient& client,
        SOCKET& acceptedSocket
    )
    {
        checkResult(
            client.disconnect(),
            "Client disconnection failed"
        );

        checkResult(
            SocketUtils::closeSocket(acceptedSocket),
            "Accepted socket cleanup failed"
        );

        checkDisconnected(client);
    }

    // Tests the initial client state.
    void testInitialState()
    {
        TcpClient client;

        checkDisconnected(client);

        checkResult(
            client.disconnect(),
            "Inactive client cleanup failed"
        );

        checkDisconnected(client);

        std::cout
            << "[PASS] Initial TCP client state test\n";
    }

    // Tests successful TCP connection establishment.
    void testConnection()
    {
        TcpListener listener;
        TcpClient client;

        SOCKET acceptedSocket = INVALID_SOCKET;

        startListener(listener);

        connectClient(
            client,
            listener,
            acceptedSocket
        );

        check(
            client.isConnected(),
            "Client connection state is invalid"
        );

        check(
            client.getSocket() != INVALID_SOCKET,
            "Connected client socket is invalid"
        );

        check(
            acceptedSocket != client.getSocket(),
            "Client and server share the same socket"
        );

        uint16_t clientPort = 0;

        checkResult(
            SocketUtils::getLocalPort(
                client.getSocket(),
                clientPort),
            "Client local port retrieval failed"
        );

        check(
            clientPort != 0 &&
            clientPort != listener.getPort(),
            "Client local port is invalid"
        );

        closeConnection(client, acceptedSocket);

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] TCP client connection test\n";
    }

    // Tests duplicate connection protection.
    void testDuplicateConnection()
    {
        TcpListener listener;
        TcpClient client;

        SOCKET acceptedSocket = INVALID_SOCKET;

        startListener(listener);

        connectClient(
            client,
            listener,
            acceptedSocket
        );

        const SOCKET originalSocket =
            client.getSocket();

        check(
            client.connectTo(
                LOCAL_ADDRESS,
                listener.getPort()
            ) == WSAEINVAL,
            "Duplicate connection was accepted"
        );

        check(
            client.isConnected(),
            "Duplicate connection disconnected the client"
        );

        check(
            client.getSocket() == originalSocket,
            "Duplicate connection replaced the socket"
        );

        closeConnection(client, acceptedSocket);

        std::cout
            << "[PASS] Duplicate client connection test\n";
    }

    // Tests invalid connection parameters and recovery.
    void testInvalidParameters()
    {
        TcpClient client;
        TcpListener listener;

        startListener(listener);

        const char* invalidAddresses[] =
        {
            nullptr,
            "",
            "invalid",
            "999.999.999.999"
        };

        for (int i = 0; i < 4; ++i)
        {
            check(
                client.connectTo(
                    invalidAddresses[i],
                    listener.getPort()
                ) == WSAEINVAL,
                "Invalid client address was accepted"
            );

            checkDisconnected(client);
        }

        check(
            client.connectTo(
                LOCAL_ADDRESS,
                0
            ) == WSAEINVAL,
            "Invalid destination port was accepted"
        );

        checkDisconnected(client);

        // Verify that the client can recover after failures.
        SOCKET acceptedSocket = INVALID_SOCKET;

        connectClient(
            client,
            listener,
            acceptedSocket
        );

        closeConnection(client, acceptedSocket);

        std::cout
            << "[PASS] Invalid client parameters test\n";
    }

    // Tests automatic non-blocking configuration.
    void testNonBlockingClient()
    {
        TcpListener listener;
        TcpClient client;

        SOCKET acceptedSocket = INVALID_SOCKET;

        startListener(listener);

        connectClient(
            client,
            listener,
            acceptedSocket
        );

        // Prevent an indefinite wait if blocking mode is incorrect.
        const DWORD receiveTimeout = 1000;

        if (setsockopt(
            client.getSocket(),
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(
                &receiveTimeout),
            static_cast<int>(
                sizeof(receiveTimeout))
        ) == SOCKET_ERROR)
        {
            checkResult(
                WSAGetLastError(),
                "Receive timeout configuration failed"
            );
        }

        char byte = 0;

        const int result = recv(
            client.getSocket(),
            &byte,
            1,
            0
        );

        check(
            result == SOCKET_ERROR,
            "Empty client socket returned unexpected data"
        );

        check(
            WSAGetLastError() == WSAEWOULDBLOCK,
            "Client socket is not non-blocking"
        );

        closeConnection(client, acceptedSocket);

        std::cout
            << "[PASS] Non-blocking TCP client test\n";
    }

    // Tests repeated connection and disconnection.
    void testReconnect()
    {
        TcpListener listener;
        TcpClient client;

        startListener(listener);

        for (int i = 0; i < 10; ++i)
        {
            SOCKET acceptedSocket = INVALID_SOCKET;

            connectClient(
                client,
                listener,
                acceptedSocket
            );

            check(
                client.isConnected(),
                "Client reconnection failed"
            );

            closeConnection(
                client,
                acceptedSocket
            );

            checkResult(
                client.disconnect(),
                "Repeated client cleanup failed"
            );

            checkDisconnected(client);
        }

        std::cout
            << "[PASS] TCP client reconnection test\n";
    }

    // Tests automatic socket cleanup by the destructor.
    void testDestructorCleanup()
    {
        TcpListener listener;

        startListener(listener);

        SOCKET acceptedSocket = INVALID_SOCKET;

        {
            TcpClient client;

            connectClient(
                client,
                listener,
                acceptedSocket
            );

            // The destructor closes the client socket.
        }

        MessageReceiver receiver;

        check(
            waitForReceive(
                receiver,
                acceptedSocket
            ) == ReceiveStatus::ConnectionClosed,
            "Client destructor did not close the connection"
        );

        checkResult(
            SocketUtils::closeSocket(acceptedSocket),
            "Accepted socket cleanup failed"
        );

        std::cout
            << "[PASS] TCP client destructor cleanup test\n";
    }

    // Tests user registration and all request priorities.
    void testUserMessages()
    {
        TcpListener listener;
        TcpClient client;

        SOCKET acceptedSocket = INVALID_SOCKET;

        startListener(listener);

        connectClient(
            client,
            listener,
            acceptedSocket
        );

        MessageReceiver receiver;
        MessageHeader header = {};

        // STEP 1: Send the user registration message.
        RegisterUrPayload registration;

        std::size_t bytesSent = 0;

        checkResult(
            MessageIO::sendMessage(
                client.getSocket(),
                registration,
                bytesSent),
            "User registration sending failed"
        );

        check(
            bytesSent == Constants::HEADER_SIZE,
            "Incorrect user registration size"
        );

        const uint8_t* payload =
            receiveExpected(
                receiver,
                acceptedSocket,
                MessageType::REGISTER_UR,
                header
            );

        check(
            header.length == 0 &&
            payload == nullptr,
            "User registration contains unexpected data"
        );

        RegisterUrPayload decodedRegistration;

        check(
            PayloadCodec::deserializeRegisterUr(
                payload,
                header.length,
                decodedRegistration),
            "User registration decoding failed"
        );

        check(
            receiver.consumeMessage(),
            "Registration consumption failed"
        );

        // STEP 2: Send requests with all supported priorities.
        const Priority priorities[] =
        {
            Priority::HIGH,
            Priority::MEDIUM,
            Priority::LOW
        };

        const uint8_t data[] =
        {
            10, 20, 30, 40, 50
        };

        for (uint64_t i = 0; i < 3; ++i)
        {
            RequestPayload request;

            request.clientRequestId = i + 1;
            request.priority = priorities[i];

            request.dataSize =
                static_cast<uint32_t>(sizeof(data));

            request.data = data;

            checkResult(
                MessageIO::sendMessage(
                    client.getSocket(),
                    request,
                    bytesSent),
                "User request sending failed"
            );

            check(
                bytesSent ==
                Constants::HEADER_SIZE +
                Constants::REQUEST_FIXED_SIZE +
                sizeof(data),
                "Incorrect user request message size"
            );

            payload = receiveExpected(
                receiver,
                acceptedSocket,
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
                decodedRequest.clientRequestId ==
                request.clientRequestId,
                "Incorrect client request ID"
            );

            check(
                decodedRequest.priority ==
                request.priority,
                "Incorrect request priority"
            );

            check(
                decodedRequest.dataSize ==
                sizeof(data),
                "Incorrect request data size"
            );

            check(
                decodedRequest.data != nullptr,
                "Decoded request data is null"
            );

            check(
                std::memcmp(
                    decodedRequest.data,
                    data,
                    sizeof(data)
                ) == 0,
                "Incorrect decoded request data"
            );

            check(
                receiver.consumeMessage(),
                "Request consumption failed"
            );
        }

        int socketError = 0;

        check(
            receiver.receive(
                acceptedSocket,
                socketError
            ) == ReceiveStatus::WouldBlock,
            "Unexpected additional user message"
        );

        closeConnection(client, acceptedSocket);

        std::cout
            << "[PASS] User registration and requests test\n";
    }
}

// Runs all TCP client tests.
void runTcpClientTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for client tests failed"
    );

    testInitialState();
    testConnection();
    testDuplicateConnection();
    testInvalidParameters();
    testNonBlockingClient();
    testReconnect();
    testDestructorCleanup();
    testUserMessages();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after client tests failed"
    );

    std::cout
        << "All TCP client tests passed.\n";
}