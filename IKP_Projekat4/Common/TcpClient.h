#pragma once

#include <winsock2.h>
#include <cstdint>

// Owns and manages a connected IPv4 TCP socket.
// Winsock must remain initialized during its lifetime.
class TcpClient
{
public:
    TcpClient() noexcept;

    ~TcpClient() noexcept;

    // Prevents duplicate socket ownership.
    TcpClient(const TcpClient&) = delete;

    TcpClient& operator=(const TcpClient&) = delete;

    TcpClient(TcpClient&&) = delete;

    TcpClient& operator=(TcpClient&&) = delete;

    // Connects to a remote TCP server.
    // Uses blocking connect and then enables non-blocking mode.
    // Returns zero on success or a Winsock error code.
    int connectTo(
        const char* ipAddress,
        uint16_t port
    );

    // Closes the owned TCP socket.
    int disconnect() noexcept;

    // Returns the local connection state.
    // Does not guarantee that the remote peer is still active.
    bool isConnected() const noexcept;

    // Returns a borrowed socket handle.
    // The caller must not close this socket directly.
    SOCKET getSocket() const noexcept;

private:
    SOCKET socket_;

    bool connected_;

    // Releases resources after a failed connection attempt.
    int cleanupAfterFailure(int error) noexcept;
};