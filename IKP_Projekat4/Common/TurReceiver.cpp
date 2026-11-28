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

// Initializes TUR in diagnostic mode.
TurReceiver::TurReceiver(
    ConnectionHandoff& handoff,
    uint32_t registrationTimeoutMs
) noexcept
    : handoff_(handoff),
    requestRegistry_(nullptr),
    connections_{},
    registrationTimeoutMs_(registrationTimeoutMs),
    nextUserSessionId_(1),
    stopEvent_(nullptr),
    thread_(nullptr),
    workingStatistics_{},
    publishedStatistics_{}
{
    InitializeCriticalSection(&statisticsLock_);
}

// Initializes TUR with a shared request registry.
TurReceiver::TurReceiver(
    ConnectionHandoff& handoff,
    RequestRegistry& requestRegistry,
    uint32_t registrationTimeoutMs
) noexcept
    : TurReceiver(handoff, registrationTimeoutMs)
{
    requestRegistry_ = &requestRegistry;
}

// Stops TUR before destroying shared resources.
TurReceiver::~TurReceiver() noexcept
{
    stop();

    if (thread_ != nullptr)
    {
        std::terminate();
    }

    DeleteCriticalSection(&statisticsLock_);
}

// Starts the listener and TUR thread.
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

    // The production receiver requires a valid registry.
    if (requestRegistry_ != nullptr &&
        !requestRegistry_->isValid())
    {
        const int error =
            requestRegistry_->getFatalError();

        return error != 0
            ? error
            : WSAEINVAL;
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

// Signals shutdown and waits for TUR completion.
int TurReceiver::stop() noexcept
{
    if (thread_ != nullptr)
    {
        if (!SetEvent(stopEvent_))
        {
            return static_cast<int>(GetLastError());
        }

        const DWORD waitResult =
            WaitForSingleObject(
                thread_,
                INFINITE
            );

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

    const int listenerResult =
        listener_.stop();

    if (listenerResult != 0)
    {
        return listenerResult;
    }

    return getStatistics().lastError;
}

// Returns the active listening port.
uint16_t TurReceiver::getPort() const noexcept
{
    return listener_.getPort();
}

// Returns a synchronized statistics snapshot.
TurStatistics TurReceiver::getStatistics()
noexcept
{
    EnterCriticalSection(&statisticsLock_);

    const TurStatistics result =
        publishedStatistics_;

    LeaveCriticalSection(&statisticsLock_);

    return result;
}

// Publishes thread-owned diagnostic statistics.
void TurReceiver::publishStatistics() noexcept
{
    EnterCriticalSection(&statisticsLock_);

    publishedStatistics_ =
        workingStatistics_;

    LeaveCriticalSection(&statisticsLock_);
}

// Checks whether shutdown was requested.
bool TurReceiver::shutdownRequested()
const noexcept
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

// Executes the TUR event loop.
unsigned TurReceiver::run()
{
    while (!shutdownRequested())
    {
        const int result = pollOnce();

        if (result != 0)
        {
            workingStatistics_.lastError =
                result;
        }

        publishStatistics();

        if (workingStatistics_.lastError != 0)
        {
            break;
        }
    }

    // Release connections still owned by TUR.
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
        static_cast<long>(
            (timeoutMs % 1000) * 1000
            );

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

    // Process existing connections.
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

        // Reject clients that fail to register in time.
        if (connection->role ==
            ConnectionRole::Pending &&
            GetTickCount64() -
            connection->acceptedAt >=
            registrationTimeoutMs_)
        {
            closeConnection(i, true);

            if (workingStatistics_.lastError != 0)
            {
                return workingStatistics_.lastError;
            }

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

    // Only TUR may accept new TCP connections.
    if (selected > 0 &&
        FD_ISSET(
            listener_.getSocket(),
            &readSet))
    {
        return acceptPending();
    }

    return 0;
}

// Accepts a bounded number of incoming connections.
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

// Receives and processes messages from one connection.
void TurReceiver::processConnection(
    std::size_t index
)
{
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

        if (status == ReceiveStatus::ConnectionClosed ||
            status == ReceiveStatus::SocketError)
        {
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

        // STEP 1: Register pending connections.
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

                // Prevent session identifier reuse after overflow.
                if (nextUserSessionId_ == 0)
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

                // Assign a unique user session identifier.
                connection->sessionId =
                    nextUserSessionId_++;

                --workingStatistics_.pendingConnections;

                ++workingStatistics_.activeUsers;

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

                connection->sessionId = 0;

                connection->needsDrain = true;

                // Transfer exclusive ownership to TWR.
                if (!handoff_.tryPush(connection))
                {
                    connection->role =
                        ConnectionRole::Pending;

                    closeConnection(index, true);

                    return;
                }

                --workingStatistics_.pendingConnections;

                ++workingStatistics_.handedOffWorkers;

                return;
            }

            closeConnection(index, true);

            return;
        }

        // STEP 2: Validate user message types.
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

        // STEP 3: Register the request before consuming
        // the receive buffer.
        const int result =
            handleRequest(
                *connection,
                request
            );

        if (result != 0)
        {
            // Preserve already committed requests and stop
            // TUR if the registry has a fatal internal error.
            if (requestRegistry_ != nullptr)
            {
                const int fatalError =
                    requestRegistry_->getFatalError();

                if (fatalError != 0)
                {
                    workingStatistics_.lastError =
                        fatalError;
                }
            }

            // Reject the connection if the request
            // cannot be accepted.
            closeConnection(index, true);

            return;
        }

        // STEP 4: Release the receive buffer only after
        // the registry owns a copy of the request data.
        if (!connection->receiver.consumeMessage())
        {
            closeConnection(index, true);

            return;
        }
    }

    connection->needsDrain = true;
}

// Registers a validated user request.
int TurReceiver::handleRequest(
    ConnectionContext& connection,
    const RequestPayload& request
)
{
    if (connection.role != ConnectionRole::User ||
        connection.sessionId == 0)
    {
        return WSAEINVAL;
    }

    // Preserve diagnostic mode for existing tests.
    if (requestRegistry_ == nullptr)
    {
        ++workingStatistics_.observedRequests;

        return 0;
    }

    uint64_t lbRequestId = 0;

    const int result =
        requestRegistry_->submit(
            connection.getSocket(),
            connection.sessionId,
            request,
            lbRequestId
        );

    if (result != 0)
    {
        return result;
    }

    ++workingStatistics_.observedRequests;

    return 0;
}

// Releases a locally owned connection.
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
        // Mark all requests from this session as disconnected
        // before releasing the underlying socket.
        if (requestRegistry_ != nullptr &&
            connection->sessionId != 0)
        {
            requestRegistry_->markUserDisconnected(
                connection->sessionId
            );
        }

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

// Finds an unused connection slot.
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