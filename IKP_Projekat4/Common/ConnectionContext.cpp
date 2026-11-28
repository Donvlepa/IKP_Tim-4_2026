#include "pch.h"

#include <winsock2.h>
#include <Windows.h>

#include "ConnectionContext.h"
#include "SocketUtils.h"

// Transfers socket ownership into the connection context.
ConnectionContext::ConnectionContext(
    SOCKET& socketHandle
) noexcept
    : role(ConnectionRole::Pending),
    workerId(0),
    sessionId(0),
    acceptedAt(GetTickCount64()),
    needsDrain(false),
    socket_(socketHandle)
{
    socketHandle = INVALID_SOCKET;
}

// Releases the owned TCP socket.
ConnectionContext::~ConnectionContext() noexcept
{
    close();
}

// Returns a borrowed socket handle.
SOCKET ConnectionContext::getSocket() const noexcept
{
    return socket_;
}

// Checks local socket ownership.
bool ConnectionContext::hasSocket() const noexcept
{
    return socket_ != INVALID_SOCKET;
}

// Closes the owned socket.
int ConnectionContext::close() noexcept
{
    return SocketUtils::closeSocket(socket_);
}