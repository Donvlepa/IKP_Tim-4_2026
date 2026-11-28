#pragma once

#include <winsock2.h>
#include <cstdint>

#include "MessageIO.h"

// Identifies the registered connection role.
enum class ConnectionRole
{
    Pending,
    User,
    Worker
};

// Owns a TCP socket and its persistent receive state.
// Only the current owner thread may access its fields.
class ConnectionContext
{
public:
    // Transfers socket ownership from the caller.
    explicit ConnectionContext(
        SOCKET& socketHandle
    ) noexcept;

    ~ConnectionContext() noexcept;

    ConnectionContext(const ConnectionContext&) = delete;

    ConnectionContext& operator=(
        const ConnectionContext&) = delete;

    ConnectionContext(ConnectionContext&&) = delete;

    ConnectionContext& operator=(
        ConnectionContext&&) = delete;

    // Returns the owned socket without transferring ownership.
    SOCKET getSocket() const noexcept;

    // Checks whether the context owns a socket.
    bool hasSocket() const noexcept;

    // Closes the owned socket.
    int close() noexcept;

    // Connection registration information.
    ConnectionRole role;

    uint64_t workerId;

    uint64_t sessionId;

    uint64_t acceptedAt;

    // Indicates that buffered messages may require processing.
    bool needsDrain;

    // Preserves the complete TCP receive state.
    MessageReceiver receiver;

private:
    SOCKET socket_;
};