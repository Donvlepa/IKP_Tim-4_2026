#include "pch.h"

#include <winsock2.h>

#include "TcpListener.h"
#include "SocketUtils.h"

#pragma comment(lib, "Ws2_32.lib")

// Initializes an inactive TCP listener.
TcpListener::TcpListener() noexcept
    : socket_(INVALID_SOCKET),
    port_(0),
    listening_(false)
{
}

// Releases the listening socket.
TcpListener::~TcpListener() noexcept
{
    stop();
}

// Creates and starts a non-blocking TCP listener.
int TcpListener::start(
    const char* ipAddress,
    uint16_t port,
    int backlog
)
{
    // Prevent starting an already active listener.
    if (socket_ != INVALID_SOCKET)
    {
        return WSAEINVAL;
    }

    if (ipAddress == nullptr ||
        ipAddress[0] == '\0' ||
        backlog <= 0)
    {
        return WSAEINVAL;
    }

    // STEP 1: Create a TCP socket.
    int result =
        SocketUtils::createTcpSocket(socket_);

    if (result != 0)
    {
        return result;
    }

    // STEP 2: Prevent another socket from binding
    // to the same local endpoint.
    const int exclusiveAddress = 1;

    result = setsockopt(
        socket_,
        SOL_SOCKET,
        SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(
            &exclusiveAddress
            ),
        static_cast<int>(
            sizeof(exclusiveAddress)
            )
    );

    if (result == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();

        return cleanupAfterFailure(error);
    }

    // STEP 3: Bind the socket to the local endpoint.
    result = SocketUtils::bindSocket(
        socket_,
        ipAddress,
        port
    );

    if (result != 0)
    {
        return cleanupAfterFailure(result);
    }

    // STEP 4: Start listening for incoming connections.
    result = SocketUtils::listenSocket(
        socket_,
        backlog
    );

    if (result != 0)
    {
        return cleanupAfterFailure(result);
    }

    // STEP 5: Enable non-blocking operation.
    result = SocketUtils::setNonBlocking(
        socket_,
        true
    );

    if (result != 0)
    {
        return cleanupAfterFailure(result);
    }

    // STEP 6: Retrieve the actual local port.
    uint16_t assignedPort = 0;

    result = SocketUtils::getLocalPort(
        socket_,
        assignedPort
    );

    if (result != 0)
    {
        return cleanupAfterFailure(result);
    }

    port_ = assignedPort;
    listening_ = true;

    return 0;
}

// Closes the listening socket.
int TcpListener::stop() noexcept
{
    const int result =
        SocketUtils::closeSocket(socket_);

    if (result != 0)
    {
        return result;
    }

    port_ = 0;
    listening_ = false;

    return 0;
}

// Checks the listener state.
bool TcpListener::isListening() const noexcept
{
    return listening_;
}

// Returns the listening socket without transferring ownership.
SOCKET TcpListener::getSocket() const noexcept
{
    return listening_
        ? socket_
        : INVALID_SOCKET;
}

// Returns the actual listening port.
uint16_t TcpListener::getPort() const noexcept
{
    return port_;
}

// Cleans up resources after an unsuccessful start operation.
int TcpListener::cleanupAfterFailure(
    int error
) noexcept
{
    const int cleanupResult = stop();

    if (cleanupResult != 0)
    {
        return cleanupResult;
    }

    return error;
}