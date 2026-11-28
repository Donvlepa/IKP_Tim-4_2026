#include "pch.h"

#include <winsock2.h>

#include "TcpClient.h"
#include "SocketUtils.h"

#pragma comment(lib, "Ws2_32.lib")

// Initializes an inactive TCP client.
TcpClient::TcpClient() noexcept
    : socket_(INVALID_SOCKET),
    connected_(false)
{
}

// Releases the owned TCP socket.
TcpClient::~TcpClient() noexcept
{
    disconnect();
}

// Connects to a remote TCP server.
int TcpClient::connectTo(
    const char* ipAddress,
    uint16_t port
)
{
    // Prevent overwriting an existing socket.
    if (socket_ != INVALID_SOCKET)
    {
        return WSAEINVAL;
    }

    if (ipAddress == nullptr ||
        ipAddress[0] == '\0' ||
        port == 0)
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

    // STEP 2: Connect to the remote server.
    result = SocketUtils::connectSocket(
        socket_,
        ipAddress,
        port
    );

    if (result != 0)
    {
        return cleanupAfterFailure(result);
    }

    // STEP 3: Enable non-blocking communication.
    result = SocketUtils::setNonBlocking(
        socket_,
        true
    );

    if (result != 0)
    {
        return cleanupAfterFailure(result);
    }

    connected_ = true;

    return 0;
}

// Closes the owned TCP socket.
int TcpClient::disconnect() noexcept
{
    const int result =
        SocketUtils::closeSocket(socket_);

    if (result != 0)
    {
        return result;
    }

    connected_ = false;

    return 0;
}

// Returns the local connection state.
bool TcpClient::isConnected() const noexcept
{
    return connected_;
}

// Returns the socket without transferring ownership.
SOCKET TcpClient::getSocket() const noexcept
{
    return connected_
        ? socket_
        : INVALID_SOCKET;
}

// Releases resources after an unsuccessful connection attempt.
int TcpClient::cleanupAfterFailure(
    int error
) noexcept
{
    const int cleanupResult = disconnect();

    if (cleanupResult != 0)
    {
        return cleanupResult;
    }

    return error;
}