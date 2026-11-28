#pragma once

#include <winsock2.h>
#include <cstdint>

namespace SocketUtils
{
    // Creates an IPv4 TCP socket.
    // The output socket must be INVALID_SOCKET.
    int createTcpSocket(SOCKET& socketHandle);

    // Binds a socket to a local IPv4 address and port.
    // Port zero allows the operating system to select a port.
    int bindSocket(
        SOCKET socketHandle,
        const char* ipAddress,
        uint16_t port
    );

    // Starts listening for incoming TCP connections.
    int listenSocket(
        SOCKET socketHandle,
        int backlog = SOMAXCONN
    );

    // Accepts an incoming TCP connection.
    // The output socket must be INVALID_SOCKET.
    int acceptSocket(
        SOCKET listenSocketHandle,
        SOCKET& clientSocketHandle
    );

    // Connects a TCP socket to a remote IPv4 endpoint.
    int connectSocket(
        SOCKET socketHandle,
        const char* ipAddress,
        uint16_t port
    );

    // Enables or disables non-blocking mode.
    int setNonBlocking(
        SOCKET socketHandle,
        bool enabled
    );

    // Retrieves the local port of a bound socket.
    int getLocalPort(
        SOCKET socketHandle,
        uint16_t& port
    );

    // Closes a socket and invalidates its handle on success.
    int closeSocket(SOCKET& socketHandle);
}