#include "pch.h"

#include "ConnectionHandoff.h"

// Initializes an empty handoff queue.
ConnectionHandoff::ConnectionHandoff() noexcept
    : entries_{},
    head_(0),
    tail_(0),
    count_(0),
    sealed_(false)
{
    InitializeCriticalSection(&lock_);
}

// Releases all connections still owned by the queue.
// All external threads must already be stopped.
ConnectionHandoff::~ConnectionHandoff() noexcept
{
    seal();

    ConnectionContext* connection = nullptr;

    while ((connection = tryPop()) != nullptr)
    {
        delete connection;
    }

    DeleteCriticalSection(&lock_);
}

// Transfers ownership into the FIFO queue.
bool ConnectionHandoff::tryPush(
    ConnectionContext*& connection
) noexcept
{
    if (connection == nullptr)
    {
        return false;
    }

    EnterCriticalSection(&lock_);

    if (sealed_ || count_ == CAPACITY)
    {
        LeaveCriticalSection(&lock_);

        return false;
    }

    entries_[tail_] = connection;

    tail_ = (tail_ + 1) % CAPACITY;

    ++count_;

    // The queue is now the exclusive owner.
    connection = nullptr;

    LeaveCriticalSection(&lock_);

    return true;
}

// Transfers ownership to the receiving thread.
ConnectionContext* ConnectionHandoff::tryPop() noexcept
{
    EnterCriticalSection(&lock_);

    if (count_ == 0)
    {
        LeaveCriticalSection(&lock_);

        return nullptr;
    }

    ConnectionContext* connection =
        entries_[head_];

    entries_[head_] = nullptr;

    head_ = (head_ + 1) % CAPACITY;

    --count_;

    LeaveCriticalSection(&lock_);

    return connection;
}

// Prevents new transfers.
void ConnectionHandoff::seal() noexcept
{
    EnterCriticalSection(&lock_);

    sealed_ = true;

    LeaveCriticalSection(&lock_);
}

// Returns the sealed state.
bool ConnectionHandoff::isSealed() noexcept
{
    EnterCriticalSection(&lock_);

    const bool result = sealed_;

    LeaveCriticalSection(&lock_);

    return result;
}

// Returns the number of pending connections.
std::size_t ConnectionHandoff::size() noexcept
{
    EnterCriticalSection(&lock_);

    const std::size_t result = count_;

    LeaveCriticalSection(&lock_);

    return result;
}

// Checks whether the queue is empty.
bool ConnectionHandoff::isEmpty() noexcept
{
    return size() == 0;
}