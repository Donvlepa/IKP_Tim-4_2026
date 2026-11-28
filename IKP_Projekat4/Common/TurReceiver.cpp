#include "pch.h"

#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <new>
#include <exception>

#include "TurReceiver.h"
#include "SocketUtils.h"
#include "PayloadCodec.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr uint32_t SELECT_TIMEOUT_MS = 50;

    constexpr int MAX_ACCEPTS_PER_POLL = 16;

    constexpr int MAX_MESSAGES_PER_POLL = 16;
}

// Initializes an inactive TUR receiver.
TurReceiver::TurReceiver(
    ConnectionHandoff& handoff,
    uint32_t registrationTimeoutMs
) noexcept
    : handoff_(handoff),
    connections_{},
    registrationTimeoutMs_(registrationTimeoutMs),
    stopEvent_(nullptr),
    thread_(nullptr),
    workingStatistics_{},
    publishedStatistics_{}
{
    InitializeCriticalSection(&statisticsLock_);
}

// Stops the thread before releasing shared resources.
TurReceiver::~TurReceiver() noexcept
{
    stop();

    // Never destroy shared state while a thread may use it.
    if (thread_ != nullptr)
    {
        std::terminate();
    }

    DeleteCriticalSection(&statisticsLock_);
}

// Starts the listener and the TUR thread.
int TurReceiver::start(
    const char* ipAddress,
    uint16_t port
)
{
    if (thread_ != nullptr ||
        stopEvent_ != nullptr ||
        listener_.isListening() ||
        registrationTimeoutMs_ == 0 ||
        handoff_.isSealed())
    {
        return WSAEINVAL;
    }

    workingStatistics_ = TurStatistics{};

    publishStatistics();

    int result = listener_.start(
        ipAddress,
        port
    );

    if (result != 0)
    {
        return result;
    }

    // Create a manual-reset shutdown event.
    stopEvent_ = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        nullptr
    );

    if (stopEvent_ == nullptr)
    {
        const int error =
            static_cast<int>(GetLastError());

        listener_.stop();

        return error;
    }

    // Start the actual TUR thread.
    const uintptr_t threadHandle =
        _beginthreadex(
            nullptr,
            0,
            threadEntry,
            this,
            0,
            nullptr
        );

    if (threadHandle == 0)
    {
        CloseHandle(stopEvent_);

        stopEvent_ = nullptr;

        listener_.stop();

        return WSAENOBUFS;
    }

    thread_ =
        reinterpret_cast<HANDLE>(threadHandle);

    return 0;
}

// Signals shutdown and joins the TUR thread.
int TurReceiver::stop() noexcept
{
    if (thread_ != nullptr)
    {
        if (!SetEvent(stopEvent_))
        {
            return static_cast<int>(GetLastError());
        }

        const DWORD waitResult =
            WaitForSingleObject(thread_, INFINITE);

        if (waitResult != WAIT_OBJECT_0)
        {
            return waitResult == WAIT_FAILED
                ? static_cast<int>(GetLastError())
                : WSAEINVAL;
        }

        CloseHandle(thread_);

        thread_ = nullptr;
    }

    if (stopEvent_ != nullptr)
    {
        CloseHandle(stopEvent_);

        stopEvent_ = nullptr;
    }

    const int listenerResult = listener_.stop();

    if (listenerResult != 0)
    {
        return listenerResult;
    }

    return getStatistics().lastError;
}

// Returns the listening port.
uint16_t TurReceiver::getPort() const noexcept
{
    return listener_.getPort();
}

// Returns a synchronized copy of the statistics.
TurStatistics TurReceiver::getStatistics() noexcept
{
    EnterCriticalSection(&statisticsLock_);

    const TurStatistics result =
        publishedStatistics_;

    LeaveCriticalSection(&statisticsLock_);

    return result;
}

// Publishes statistics without exposing thread-owned data.
void TurReceiver::publishStatistics() noexcept
{
    EnterCriticalSection(&statisticsLock_);

    publishedStatistics_ = workingStatistics_;

    LeaveCriticalSection(&statisticsLock_);
}

// Checks the shutdown event.
bool TurReceiver::shutdownRequested() const noexcept
{
    return WaitForSingleObject(
        stopEvent_,
        0
    ) == WAIT_OBJECT_0;
}

// Windows thread entry point.
unsigned __stdcall TurReceiver::threadEntry(
    void* parameter
)
{
    TurReceiver* receiver =
        static_cast<TurReceiver*>(parameter);

    return receiver->run();
}

// Runs the TUR event loop.
unsigned TurReceiver::run()
{
    while (!shutdownRequested())
    {
        const int result = pollOnce();

        if (result != 0)
        {
            workingStatistics_.lastError = result;
        }

        publishStatistics();

        if (workingStatistics_.lastError != 0)
        {
            break;
        }
    }

    // Close only connections still owned by TUR.
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        closeConnection(i, false);
    }

    publishStatistics();

    return workingStatistics_.lastError == 0
        ? 0
        : 1;
}

// Processes one select iteration.
int TurReceiver::pollOnce()
{
    fd_set readSet;

    FD_ZERO(&readSet);

    FD_SET(
        listener_.getSocket(),
        &readSet
    );

    bool hasBufferedWork = false;

    // Add all locally owned sockets.
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        ConnectionContext* connection =
            connections_[i];

        if (connection == nullptr)
        {
            continue;
        }

        FD_SET(
            connection->getSocket(),
            &readSet
        );

        if (connection->needsDrain)
        {
            hasBufferedWork = true;
        }
    }

    const uint32_t timeoutMs =
        hasBufferedWork
        ? 0
        : SELECT_TIMEOUT_MS;

    timeval timeout = {};

    timeout.tv_sec =
        static_cast<long>(timeoutMs / 1000);

    timeout.tv_usec =
        static_cast<long>((timeoutMs % 1000) * 1000);

    const int selected = select(
        0,
        &readSet,
        nullptr,
        nullptr,
        &timeout
    );

    if (selected == SOCKET_ERROR)
    {
        return WSAGetLastError();
    }

    if (shutdownRequested())
    {
        return 0;
    }

    // Process existing connections first.
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        if (shutdownRequested())
        {
            return 0;
        }

        ConnectionContext* connection =
            connections_[i];

        if (connection == nullptr)
        {
            continue;
        }

        // Reject clients that do not register in time.
        if (connection->role ==
            ConnectionRole::Pending &&
            GetTickCount64() -
            connection->acceptedAt >=
            registrationTimeoutMs_)
        {
            closeConnection(i, true);

            continue;
        }

        const bool socketReady =
            selected > 0 &&
            FD_ISSET(
                connection->getSocket(),
                &readSet
            );

        if (socketReady ||
            connection->needsDrain)
        {
            processConnection(i);
        }

        if (workingStatistics_.lastError != 0)
        {
            return workingStatistics_.lastError;
        }
    }

    // TUR is the only thread allowed to accept connections.
    if (selected > 0 &&
        FD_ISSET(
            listener_.getSocket(),
            &readSet))
    {
        return acceptPending();
    }

    return 0;
}

// Accepts a bounded number of new connections.
int TurReceiver::acceptPending()
{
    for (int i = 0;
        i < MAX_ACCEPTS_PER_POLL;
        ++i)
    {
        if (shutdownRequested())
        {
            return 0;
        }

        SOCKET acceptedSocket = INVALID_SOCKET;

        int result = SocketUtils::acceptSocket(
            listener_.getSocket(),
            acceptedSocket
        );

        if (result == WSAEWOULDBLOCK)
        {
            return 0;
        }

        if (result == WSAECONNRESET)
        {
            continue;
        }

        if (result != 0)
        {
            return result;
        }

        const std::size_t slot =
            findFreeSlot();

        if (slot == MAX_CONNECTIONS)
        {
            ++workingStatistics_.rejectedConnections;

            result = SocketUtils::closeSocket(
                acceptedSocket
            );

            if (result != 0)
            {
                return result;
            }

            continue;
        }

        result = SocketUtils::setNonBlocking(
            acceptedSocket,
            true
        );

        if (result != 0)
        {
            const int cleanupResult =
                SocketUtils::closeSocket(
                    acceptedSocket
                );

            return cleanupResult != 0
                ? cleanupResult
                : result;
        }

        ConnectionContext* connection =
            new (std::nothrow)
            ConnectionContext(acceptedSocket);

        if (connection == nullptr)
        {
            const int cleanupResult =
                SocketUtils::closeSocket(
                    acceptedSocket
                );

            return cleanupResult != 0
                ? cleanupResult
                : WSAENOBUFS;
        }

        connections_[slot] = connection;

        ++workingStatistics_.pendingConnections;
    }

    return 0;
}

// Processes messages from one locally owned connection.
void TurReceiver::processConnection(
    std::size_t index
)
{
    // A reference allows ownership transfer to clear the slot.
    ConnectionContext*& connection =
        connections_[index];

    if (connection == nullptr)
    {
        return;
    }

    connection->needsDrain = false;

    for (int i = 0;
        i < MAX_MESSAGES_PER_POLL;
        ++i)
    {
        if (shutdownRequested())
        {
            return;
        }

        int socketError = 0;

        const ReceiveStatus status =
            connection->receiver.receive(
                connection->getSocket(),
                socketError
            );

        if (status == ReceiveStatus::WouldBlock)
        {
            return;
        }

        if (status == ReceiveStatus::ConnectionClosed)
        {
            closeConnection(index, false);

            return;
        }

        if (status == ReceiveStatus::SocketError)
        {
            // A client socket error must not stop TUR.
            closeConnection(index, false);

            return;
        }

        if (status != ReceiveStatus::MessageReady)
        {
            closeConnection(index, true);

            return;
        }

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        if (!connection->receiver.getMessage(
            header,
            payload))
        {
            closeConnection(index, true);

            return;
        }

        // STEP 1: Classify an unregistered connection.
        if (connection->role ==
            ConnectionRole::Pending)
        {
            if (header.type ==
                MessageType::REGISTER_UR)
            {
                RegisterUrPayload registration;

                if (!PayloadCodec::deserializeRegisterUr(
                    payload,
                    header.length,
                    registration))
                {
                    closeConnection(index, true);

                    return;
                }

                if (!connection->receiver.consumeMessage())
                {
                    closeConnection(index, true);

                    return;
                }

                connection->role =
                    ConnectionRole::User;

                --workingStatistics_.pendingConnections;

                ++workingStatistics_.activeUsers;

                // Continue processing buffered user messages.
                continue;
            }

            if (header.type ==
                MessageType::REGISTER_WR)
            {
                RegisterWrPayload registration;

                if (!PayloadCodec::deserializeRegisterWr(
                    payload,
                    header.length,
                    registration))
                {
                    closeConnection(index, true);

                    return;
                }

                if (registration.workerId == 0)
                {
                    closeConnection(index, true);

                    return;
                }

                if (!connection->receiver.consumeMessage())
                {
                    closeConnection(index, true);

                    return;
                }

                connection->role =
                    ConnectionRole::Worker;

                connection->workerId =
                    registration.workerId;

                // TWR will assign the final session identifier.
                connection->sessionId = 0;

                // TWR must drain potentially buffered messages
                // before waiting for socket readiness.
                connection->needsDrain = true;

                // STEP 2: Transfer exclusive ownership to TWR.
                if (!handoff_.tryPush(connection))
                {
                    connection->role =
                        ConnectionRole::Pending;

                    closeConnection(index, true);

                    return;
                }

                // On success, connections_[index] is nullptr.
                // TUR must never access the transferred object.
                --workingStatistics_.pendingConnections;

                ++workingStatistics_.handedOffWorkers;

                return;
            }

            // Registration must be the first protocol message.
            closeConnection(index, true);

            return;
        }

        // STEP 3: Process registered user requests.
        if (connection->role != ConnectionRole::User ||
            header.type != MessageType::REQUEST)
        {
            closeConnection(index, true);

            return;
        }

        RequestPayload request;

        if (!PayloadCodec::deserializeRequest(
            payload,
            header.length,
            request))
        {
            closeConnection(index, true);

            return;
        }

        // Request Registry and priority queues
        // will be connected in phase 8.4.
        ++workingStatistics_.observedRequests;

        if (!connection->receiver.consumeMessage())
        {
            closeConnection(index, true);

            return;
        }
    }

    // Continue processing buffered messages in the next iteration.
    connection->needsDrain = true;
}

// Releases one locally owned connection.
void TurReceiver::closeConnection(
    std::size_t index,
    bool rejected
) noexcept
{
    ConnectionContext* connection =
        connections_[index];

    if (connection == nullptr)
    {
        return;
    }

    if (connection->role == ConnectionRole::User)
    {
        --workingStatistics_.activeUsers;
    }
    else
    {
        --workingStatistics_.pendingConnections;
    }

    if (rejected)
    {
        ++workingStatistics_.rejectedConnections;
    }

    const int result = connection->close();

    if (result != 0 &&
        workingStatistics_.lastError == 0)
    {
        workingStatistics_.lastError = result;
    }

    delete connection;

    connections_[index] = nullptr;
}

// Finds an available connection slot.
std::size_t TurReceiver::findFreeSlot()
const noexcept
{
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        if (connections_[i] == nullptr)
        {
            return i;
        }
    }

    return MAX_CONNECTIONS;
}