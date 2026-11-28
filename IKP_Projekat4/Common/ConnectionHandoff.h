#pragma once

#include <winsock2.h>
#include <Windows.h>

#include <cstddef>

#include "ConnectionContext.h"

// Thread-safe bounded FIFO for transferring
// connection ownership between receiver threads.
class ConnectionHandoff
{
public:
    static constexpr std::size_t CAPACITY =
        FD_SETSIZE - 1;

    ConnectionHandoff() noexcept;

    ~ConnectionHandoff() noexcept;

    ConnectionHandoff(const ConnectionHandoff&) = delete;

    ConnectionHandoff& operator=(
        const ConnectionHandoff&) = delete;

    ConnectionHandoff(ConnectionHandoff&&) = delete;

    ConnectionHandoff& operator=(
        ConnectionHandoff&&) = delete;

    // Transfers ownership into the queue.
    // On success, connection becomes nullptr.
    // On failure, ownership remains with the caller.
    bool tryPush(
        ConnectionContext*& connection
    ) noexcept;

    // Removes the oldest connection.
    // The caller receives ownership of the returned object.
    // Returns nullptr if the queue is empty.
    ConnectionContext* tryPop() noexcept;

    // Prevents new transfers while allowing pending
    // connections to be removed.
    void seal() noexcept;

    // Returns whether new transfers are disabled.
    bool isSealed() noexcept;

    // Returns the current number of pending connections.
    std::size_t size() noexcept;

    bool isEmpty() noexcept;

private:
    ConnectionContext* entries_[CAPACITY];

    std::size_t head_;

    std::size_t tail_;

    std::size_t count_;

    bool sealed_;

    CRITICAL_SECTION lock_;
};