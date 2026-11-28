#pragma once

#include <winsock2.h>
#include <cstdint>

// Owns and manages a listening IPv4 TCP socket.
// Winsock must remain initialized during its lifetime.
class TcpListener
{
public:
    TcpListener() noexcept;

    ~TcpListener() noexcept;

    // Prevents duplicate ownership of the listening socket.
    TcpListener(const TcpListener&) = delete;

    TcpListener& operator=(const TcpListener&) = delete;

    TcpListener(TcpListener&&) = delete;

    TcpListener& operator=(TcpListener&&) = delete;

    // Creates and starts a non-blocking TCP listener.
    // Port zero allows automatic port assignment.
    // Returns zero on success or a Winsock error code.
    int start(
        const char* ipAddress,
        uint16_t port,
        int backlog = SOMAXCONN
    );

    // Closes the listening socket.
    int stop() noexcept;

    // Checks whether the server is listening.
    bool isListening() const noexcept;

    // Returns a borrowed socket handle.
    // The caller must not close this socket directly.
    SOCKET getSocket() const noexcept;

    // Returns the actual local listening port.
    uint16_t getPort() const noexcept;

private:
    SOCKET socket_;

    uint16_t port_;

    bool listening_;

    // Releases resources after a failed start operation.
    int cleanupAfterFailure(int error) noexcept;
};