#include "pch.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "SocketUtils.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    // Converts an IPv4 address and port into a socket address.
    int buildAddress(
        const char* ipAddress,
        uint16_t port,
        sockaddr_in& address
    )
    {
        if (ipAddress == nullptr || ipAddress[0] == '\0')
        {
            return WSAEINVAL;
        }

        address = {};

        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        const int result = InetPtonA(
            AF_INET,
            ipAddress,
            &address.sin_addr
        );

        if (result == 0)
        {
            return WSAEINVAL;
        }

        if (result == -1)
        {
            return WSAGetLastError();
        }

        return 0;
    }
}

namespace SocketUtils
{
    // Creates an IPv4 TCP socket.
    int createTcpSocket(SOCKET& socketHandle)
    {
        // Prevent overwriting an existing socket.
        if (socketHandle != INVALID_SOCKET)
        {
            return WSAEINVAL;
        }

        const SOCKET createdSocket = socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

        if (createdSocket == INVALID_SOCKET)
        {
            return WSAGetLastError();
        }

        socketHandle = createdSocket;

        return 0;
    }

    // Binds a socket to a local IPv4 endpoint.
    int bindSocket(
        SOCKET socketHandle,
        const char* ipAddress,
        uint16_t port
    )
    {
        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        sockaddr_in address = {};

        const int addressResult =
            buildAddress(ipAddress, port, address);

        if (addressResult != 0)
        {
            return addressResult;
        }

        const int result = bind(
            socketHandle,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address))
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        return 0;
    }

    // Starts listening for incoming connections.
    int listenSocket(
        SOCKET socketHandle,
        int backlog
    )
    {
        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        if (backlog <= 0)
        {
            return WSAEINVAL;
        }

        const int result = listen(
            socketHandle,
            backlog
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        return 0;
    }

    // Accepts a new TCP connection.
    int acceptSocket(
        SOCKET listenSocketHandle,
        SOCKET& clientSocketHandle
    )
    {
        if (listenSocketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        // Prevent overwriting an existing client socket.
        if (clientSocketHandle != INVALID_SOCKET)
        {
            return WSAEINVAL;
        }

        const SOCKET acceptedSocket = accept(
            listenSocketHandle,
            nullptr,
            nullptr
        );

        if (acceptedSocket == INVALID_SOCKET)
        {
            return WSAGetLastError();
        }

        clientSocketHandle = acceptedSocket;

        return 0;
    }

    // Connects a socket to a remote IPv4 endpoint.
    int connectSocket(
        SOCKET socketHandle,
        const char* ipAddress,
        uint16_t port
    )
    {
        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        if (port == 0)
        {
            return WSAEINVAL;
        }

        sockaddr_in address = {};

        const int addressResult =
            buildAddress(ipAddress, port, address);

        if (addressResult != 0)
        {
            return addressResult;
        }

        const int result = connect(
            socketHandle,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address))
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        return 0;
    }

    // Configures blocking or non-blocking socket operation.
    int setNonBlocking(
        SOCKET socketHandle,
        bool enabled
    )
    {
        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        u_long mode = enabled ? 1UL : 0UL;

        const int result = ioctlsocket(
            socketHandle,
            FIONBIO,
            &mode
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        return 0;
    }

    // Retrieves the local port in host byte order.
    int getLocalPort(
        SOCKET socketHandle,
        uint16_t& port
    )
    {
        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        sockaddr_in address = {};

        int addressSize =
            static_cast<int>(sizeof(address));

        const int result = getsockname(
            socketHandle,
            reinterpret_cast<sockaddr*>(&address),
            &addressSize
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        if (addressSize < static_cast<int>(sizeof(address)) ||
            address.sin_family != AF_INET)
        {
            return WSAEAFNOSUPPORT;
        }

        port = ntohs(address.sin_port);

        return 0;
    }

    // Closes a socket without allowing repeated close operations.
    int closeSocket(SOCKET& socketHandle)
    {
        if (socketHandle == INVALID_SOCKET)
        {
            return 0;
        }

        const int result = closesocket(socketHandle);

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        socketHandle = INVALID_SOCKET;

        return 0;
    }
}