#include <winsock2.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "TestSuites.h"

// Checks a test condition.
static void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";

        std::abort();
    }
}

// Checks the result of a socket operation.
static void checkResult(int result, const char* message)
{
    if (result != 0)
    {
        std::cerr << "[FAIL] " << message
            << " | Winsock error: "
            << result << "\n";

        std::abort();
    }
}

// Creates a local listening socket for testing.
static uint16_t createTestServer(SOCKET& serverSocket)
{
    checkResult(
        SocketUtils::createTcpSocket(serverSocket),
        "Server socket creation failed"
    );

    checkResult(
        SocketUtils::bindSocket(
            serverSocket, "127.0.0.1", 0),
        "Server socket binding failed"
    );

    checkResult(
        SocketUtils::listenSocket(serverSocket),
        "Server socket listening failed"
    );

    uint16_t port = 0;

    checkResult(
        SocketUtils::getLocalPort(serverSocket, port),
        "Server port retrieval failed"
    );

    check(
        port != 0,
        "Operating system did not assign a port"
    );

    return port;
}

// Waits until an incoming connection is available.
static void waitForConnection(SOCKET serverSocket)
{
    fd_set readSet;

    FD_ZERO(&readSet);
    FD_SET(serverSocket, &readSet);

    timeval timeout = {};

    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    const int result = select(
        0,
        &readSet,
        nullptr,
        nullptr,
        &timeout
    );

    check(
        result == 1 &&
        FD_ISSET(serverSocket, &readSet),
        "Incoming connection was not detected"
    );
}

// Tests TCP socket creation.
static void testSocketCreation()
{
    SOCKET socketHandle = INVALID_SOCKET;

    checkResult(
        SocketUtils::createTcpSocket(socketHandle),
        "TCP socket creation failed"
    );

    check(
        socketHandle != INVALID_SOCKET,
        "Created socket is invalid"
    );

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "TCP socket cleanup failed"
    );

    check(
        socketHandle == INVALID_SOCKET,
        "Closed socket was not invalidated"
    );

    std::cout << "[PASS] TCP socket creation test\n";
}

// Tests protection against overwriting an existing socket.
static void testDuplicateSocketCreation()
{
    SOCKET socketHandle = INVALID_SOCKET;

    checkResult(
        SocketUtils::createTcpSocket(socketHandle),
        "Initial socket creation failed"
    );

    const SOCKET originalSocket = socketHandle;

    check(
        SocketUtils::createTcpSocket(socketHandle)
        == WSAEINVAL,
        "Existing socket was overwritten"
    );

    check(
        socketHandle == originalSocket,
        "Original socket handle was modified"
    );

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "Socket cleanup failed"
    );

    std::cout << "[PASS] Duplicate socket creation test\n";
}

// Tests repeated socket closing.
static void testRepeatedClose()
{
    SOCKET socketHandle = INVALID_SOCKET;

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "Closing an invalid socket failed"
    );

    checkResult(
        SocketUtils::createTcpSocket(socketHandle),
        "Socket creation failed"
    );

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "First socket close failed"
    );

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "Second socket close failed"
    );

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "Third socket close failed"
    );

    check(
        socketHandle == INVALID_SOCKET,
        "Closed socket handle is invalid"
    );

    std::cout << "[PASS] Repeated socket close test\n";
}

// Tests binding and local port retrieval.
static void testBindAndLocalPort()
{
    SOCKET serverSocket = INVALID_SOCKET;

    checkResult(
        SocketUtils::createTcpSocket(serverSocket),
        "Socket creation failed"
    );

    checkResult(
        SocketUtils::bindSocket(
            serverSocket, "127.0.0.1", 0),
        "Socket binding failed"
    );

    uint16_t firstPort = 0;
    uint16_t secondPort = 0;

    checkResult(
        SocketUtils::getLocalPort(
            serverSocket, firstPort),
        "First local port retrieval failed"
    );

    checkResult(
        SocketUtils::getLocalPort(
            serverSocket, secondPort),
        "Second local port retrieval failed"
    );

    check(
        firstPort != 0,
        "Assigned port is zero"
    );

    check(
        firstPort == secondPort,
        "Local port unexpectedly changed"
    );

    checkResult(
        SocketUtils::closeSocket(serverSocket),
        "Bound socket cleanup failed"
    );

    std::cout << "[PASS] Bind and local port test\n";
}

// Tests creation of a listening socket.
static void testListenSocket()
{
    SOCKET serverSocket = INVALID_SOCKET;

    const uint16_t port =
        createTestServer(serverSocket);

    check(
        serverSocket != INVALID_SOCKET,
        "Listening socket is invalid"
    );

    check(
        port != 0,
        "Listening port is invalid"
    );

    checkResult(
        SocketUtils::closeSocket(serverSocket),
        "Listening socket cleanup failed"
    );

    std::cout << "[PASS] Listening socket test\n";
}

// Tests a complete TCP connection lifecycle.
static void testConnectAndAccept()
{
    SOCKET serverSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;
    SOCKET acceptedSocket = INVALID_SOCKET;

    const uint16_t port =
        createTestServer(serverSocket);

    checkResult(
        SocketUtils::createTcpSocket(clientSocket),
        "Client socket creation failed"
    );

    checkResult(
        SocketUtils::connectSocket(
            clientSocket, "127.0.0.1", port),
        "Client connection failed"
    );

    waitForConnection(serverSocket);

    checkResult(
        SocketUtils::acceptSocket(
            serverSocket, acceptedSocket),
        "Server connection acceptance failed"
    );

    check(
        acceptedSocket != INVALID_SOCKET,
        "Accepted socket is invalid"
    );

    check(
        acceptedSocket != serverSocket,
        "Accepted socket equals listening socket"
    );

    check(
        acceptedSocket != clientSocket,
        "Accepted socket equals client socket"
    );

    uint16_t acceptedPort = 0;

    checkResult(
        SocketUtils::getLocalPort(
            acceptedSocket, acceptedPort),
        "Accepted socket port retrieval failed"
    );

    check(
        acceptedPort == port,
        "Accepted socket has an incorrect local port"
    );

    uint16_t clientPort = 0;

    checkResult(
        SocketUtils::getLocalPort(
            clientSocket, clientPort),
        "Client port retrieval failed"
    );

    check(
        clientPort != 0 && clientPort != port,
        "Invalid client port"
    );

    checkResult(
        SocketUtils::closeSocket(acceptedSocket),
        "Accepted socket cleanup failed"
    );

    checkResult(
        SocketUtils::closeSocket(clientSocket),
        "Client socket cleanup failed"
    );

    checkResult(
        SocketUtils::closeSocket(serverSocket),
        "Server socket cleanup failed"
    );

    std::cout << "[PASS] Connect and accept test\n";
}

// Tests accepting multiple independent TCP clients.
static void testMultipleConnections()
{
    constexpr int CLIENT_COUNT = 5;

    SOCKET serverSocket = INVALID_SOCKET;

    SOCKET clients[CLIENT_COUNT];
    SOCKET acceptedSockets[CLIENT_COUNT];

    for (int i = 0; i < CLIENT_COUNT; ++i)
    {
        clients[i] = INVALID_SOCKET;
        acceptedSockets[i] = INVALID_SOCKET;
    }

    const uint16_t port =
        createTestServer(serverSocket);

    for (int i = 0; i < CLIENT_COUNT; ++i)
    {
        checkResult(
            SocketUtils::createTcpSocket(clients[i]),
            "Client creation failed"
        );

        checkResult(
            SocketUtils::connectSocket(
                clients[i], "127.0.0.1", port),
            "Client connection failed"
        );

        waitForConnection(serverSocket);

        checkResult(
            SocketUtils::acceptSocket(
                serverSocket, acceptedSockets[i]),
            "Client acceptance failed"
        );

        check(
            acceptedSockets[i] != INVALID_SOCKET,
            "Accepted socket is invalid"
        );

        check(
            acceptedSockets[i] != serverSocket,
            "Accepted socket equals listening socket"
        );

        for (int j = 0; j < i; ++j)
        {
            check(
                acceptedSockets[i] != acceptedSockets[j],
                "Duplicate accepted socket handle"
            );
        }
    }

    for (int i = 0; i < CLIENT_COUNT; ++i)
    {
        checkResult(
            SocketUtils::closeSocket(acceptedSockets[i]),
            "Accepted socket cleanup failed"
        );

        checkResult(
            SocketUtils::closeSocket(clients[i]),
            "Client socket cleanup failed"
        );
    }

    checkResult(
        SocketUtils::closeSocket(serverSocket),
        "Server socket cleanup failed"
    );

    std::cout << "[PASS] Multiple connections test\n";
}

// Tests blocking and non-blocking socket modes.
static void testNonBlockingMode()
{
    SOCKET serverSocket = INVALID_SOCKET;
    SOCKET acceptedSocket = INVALID_SOCKET;

    const uint16_t port =
        createTestServer(serverSocket);

    (void)port;

    checkResult(
        SocketUtils::setNonBlocking(serverSocket, true),
        "Enabling non-blocking mode failed"
    );

    check(
        SocketUtils::acceptSocket(
            serverSocket, acceptedSocket)
        == WSAEWOULDBLOCK,
        "Empty non-blocking accept returned wrong error"
    );

    check(
        acceptedSocket == INVALID_SOCKET,
        "Non-blocking accept created an invalid connection"
    );

    checkResult(
        SocketUtils::setNonBlocking(serverSocket, false),
        "Restoring blocking mode failed"
    );

    checkResult(
        SocketUtils::setNonBlocking(serverSocket, true),
        "Restoring non-blocking mode failed"
    );

    check(
        SocketUtils::acceptSocket(
            serverSocket, acceptedSocket)
        == WSAEWOULDBLOCK,
        "Repeated non-blocking accept failed"
    );

    checkResult(
        SocketUtils::closeSocket(serverSocket),
        "Non-blocking socket cleanup failed"
    );

    std::cout << "[PASS] Non-blocking socket test\n";
}

// Tests protection of an existing accept output socket.
static void testAcceptOutputProtection()
{
    SOCKET serverSocket = INVALID_SOCKET;
    SOCKET outputSocket = INVALID_SOCKET;

    const uint16_t port =
        createTestServer(serverSocket);

    (void)port;

    checkResult(
        SocketUtils::setNonBlocking(serverSocket, true),
        "Enabling non-blocking mode failed"
    );

    checkResult(
        SocketUtils::createTcpSocket(outputSocket),
        "Output socket creation failed"
    );

    const SOCKET originalSocket = outputSocket;

    check(
        SocketUtils::acceptSocket(
            serverSocket, outputSocket)
        == WSAEINVAL,
        "Existing accept output socket was accepted"
    );

    check(
        outputSocket == originalSocket,
        "Existing socket handle was overwritten"
    );

    checkResult(
        SocketUtils::closeSocket(outputSocket),
        "Output socket cleanup failed"
    );

    checkResult(
        SocketUtils::closeSocket(serverSocket),
        "Server socket cleanup failed"
    );

    std::cout << "[PASS] Accept output protection test\n";
}

// Tests operations using invalid socket handles.
static void testInvalidSocketHandles()
{
    SOCKET invalidSocket = INVALID_SOCKET;
    SOCKET acceptedSocket = INVALID_SOCKET;

    uint16_t port = 1234;

    check(
        SocketUtils::bindSocket(
            invalidSocket, "127.0.0.1", 0)
        == WSAENOTSOCK,
        "Invalid bind socket accepted"
    );

    check(
        SocketUtils::listenSocket(invalidSocket)
        == WSAENOTSOCK,
        "Invalid listen socket accepted"
    );

    check(
        SocketUtils::acceptSocket(
            invalidSocket, acceptedSocket)
        == WSAENOTSOCK,
        "Invalid accept socket accepted"
    );

    check(
        SocketUtils::connectSocket(
            invalidSocket, "127.0.0.1", 5059)
        == WSAENOTSOCK,
        "Invalid connect socket accepted"
    );

    check(
        SocketUtils::setNonBlocking(invalidSocket, true)
        == WSAENOTSOCK,
        "Invalid non-blocking socket accepted"
    );

    check(
        SocketUtils::getLocalPort(invalidSocket, port)
        == WSAENOTSOCK,
        "Invalid local port socket accepted"
    );

    check(
        port == 1234,
        "Failed port retrieval modified output"
    );

    check(
        acceptedSocket == INVALID_SOCKET,
        "Failed accept modified output"
    );

    std::cout << "[PASS] Invalid socket handles test\n";
}

// Tests invalid address and port parameters.
static void testInvalidParameters()
{
    SOCKET socketHandle = INVALID_SOCKET;

    checkResult(
        SocketUtils::createTcpSocket(socketHandle),
        "Socket creation failed"
    );

    const char* invalidAddresses[] =
    {
        nullptr,
        "",
        "invalid",
        "999.999.999.999",
        "127.0.0"
    };

    for (int i = 0; i < 5; ++i)
    {
        check(
            SocketUtils::bindSocket(
                socketHandle, invalidAddresses[i], 0)
            == WSAEINVAL,
            "Invalid bind address accepted"
        );

        check(
            SocketUtils::connectSocket(
                socketHandle, invalidAddresses[i], 5059)
            == WSAEINVAL,
            "Invalid connect address accepted"
        );
    }

    check(
        SocketUtils::connectSocket(
            socketHandle, "127.0.0.1", 0)
        == WSAEINVAL,
        "Invalid destination port accepted"
    );

    check(
        SocketUtils::listenSocket(socketHandle, 0)
        == WSAEINVAL,
        "Zero listen backlog accepted"
    );

    check(
        SocketUtils::listenSocket(socketHandle, -1)
        == WSAEINVAL,
        "Negative listen backlog accepted"
    );

    // Verify that valid operations still work.
    checkResult(
        SocketUtils::bindSocket(
            socketHandle, "127.0.0.1", 0),
        "Socket became unusable after invalid parameters"
    );

    checkResult(
        SocketUtils::listenSocket(socketHandle),
        "Listen failed after invalid parameters"
    );

    checkResult(
        SocketUtils::closeSocket(socketHandle),
        "Socket cleanup failed"
    );

    std::cout << "[PASS] Invalid socket parameters test\n";
}

// Tests repeated creation and destruction of TCP sockets.
static void testRepeatedSocketLifecycle()
{
    constexpr int ITERATION_COUNT = 1000;

    SOCKET socketHandle = INVALID_SOCKET;

    for (int i = 0; i < ITERATION_COUNT; ++i)
    {
        checkResult(
            SocketUtils::createTcpSocket(socketHandle),
            "Repeated socket creation failed"
        );

        checkResult(
            SocketUtils::closeSocket(socketHandle),
            "Repeated socket close failed"
        );

        check(
            socketHandle == INVALID_SOCKET,
            "Closed socket was not invalidated"
        );
    }

    std::cout << "[PASS] Repeated socket lifecycle test\n";
}

// Runs all socket utility tests.
void runSocketUtilsTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for socket tests failed"
    );

    testSocketCreation();
    testDuplicateSocketCreation();
    testRepeatedClose();
    testBindAndLocalPort();
    testListenSocket();
    testConnectAndAccept();
    testMultipleConnections();
    testNonBlockingMode();
    testAcceptOutputProtection();
    testInvalidSocketHandles();
    testInvalidParameters();
    testRepeatedSocketLifecycle();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after socket tests failed"
    );

    std::cout << "All socket utility tests passed.\n";
}