#include "pch.h"

#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <new>

#include "RegistrationServer.h"
#include "SocketUtils.h"
#include "PayloadCodec.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    // Limits work per polling iteration.
    constexpr int MAX_ACCEPTS_PER_POLL = 16;

    constexpr int MAX_MESSAGES_PER_POLL = 16;
}

// Initializes an inactive registration server.
RegistrationServer::RegistrationServer(
    uint32_t registrationTimeoutMs
) noexcept
    : connections_{},
    registrationTimeoutMs_(registrationTimeoutMs),
    nextSessionId_(1),
    observedRequestCount_(0)
{
}

// Releases the owned network resources.
RegistrationServer::~RegistrationServer()
{
    stop();
}

// Starts the server.
int RegistrationServer::start(
    const char* ipAddress,
    uint16_t port
)
{
    if (isRunning() ||
        getConnectionCount() != 0 ||
        registrationTimeoutMs_ == 0)
    {
        return WSAEINVAL;
    }

    const int result =
        listener_.start(ipAddress, port);

    if (result == 0)
    {
        observedRequestCount_ = 0;
    }

    return result;
}

// Processes incoming connections and messages.
int RegistrationServer::poll(uint32_t timeoutMs)
{
    if (!isRunning())
    {
        return WSAEINVAL;
    }

    fd_set readSet;

    FD_ZERO(&readSet);

    FD_SET(listener_.getSocket(), &readSet);

    bool hasPendingWork = false;

    // Build the socket set for this polling iteration.
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        Connection* connection = connections_[i];

        if (connection == nullptr)
        {
            continue;
        }

        FD_SET(connection->socket, &readSet);

        if (connection->needsDrain)
        {
            hasPendingWork = true;
        }
    }

    // Buffered messages must not wait for socket readiness.
    if (hasPendingWork)
    {
        timeoutMs = 0;
    }

    // Keep registration and shutdown checks responsive.
    if (timeoutMs > 100)
    {
        timeoutMs = 100;
    }

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

    // Process existing connections first.
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        Connection* connection = connections_[i];

        if (connection == nullptr)
        {
            continue;
        }

        // Reject clients that fail to register in time.
        if (connection->role ==
            ConnectionRole::Pending &&
            GetTickCount64() - connection->acceptedAt >=
            registrationTimeoutMs_)
        {
            std::cout
                << "[LB] Registration timeout.\n";

            const int result = closeConnection(i);

            if (result != 0)
            {
                return result;
            }

            continue;
        }

        const bool socketReady =
            selected > 0 &&
            FD_ISSET(connection->socket, &readSet);

        if (!socketReady && !connection->needsDrain)
        {
            continue;
        }

        if (!processConnection(*connection))
        {
            const int result = closeConnection(i);

            if (result != 0)
            {
                return result;
            }
        }
    }

    // Accept new connections after processing old ones.
    if (selected > 0 &&
        FD_ISSET(listener_.getSocket(), &readSet))
    {
        return acceptPending();
    }

    return 0;
}

// Accepts pending TCP connections.
int RegistrationServer::acceptPending()
{
    for (int i = 0;
        i < MAX_ACCEPTS_PER_POLL;
        ++i)
    {
        SOCKET acceptedSocket = INVALID_SOCKET;

        int result = SocketUtils::acceptSocket(
            listener_.getSocket(),
            acceptedSocket
        );

        if (result == WSAEWOULDBLOCK)
        {
            return 0;
        }

        if (result != 0)
        {
            return result;
        }

        const std::size_t slot = findFreeSlot();

        // Reject connections when the server is full.
        if (slot == MAX_CONNECTIONS)
        {
            std::cout
                << "[LB] Connection capacity reached.\n";

            result =
                SocketUtils::closeSocket(acceptedSocket);

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
                SocketUtils::closeSocket(acceptedSocket);

            return cleanupResult != 0
                ? cleanupResult
                : result;
        }

        Connection* connection =
            new (std::nothrow) Connection();

        if (connection == nullptr)
        {
            const int cleanupResult =
                SocketUtils::closeSocket(acceptedSocket);

            return cleanupResult != 0
                ? cleanupResult
                : WSAENOBUFS;
        }

        connection->socket = acceptedSocket;

        connection->acceptedAt = GetTickCount64();

        connections_[slot] = connection;

        std::cout
            << "[LB] New TCP connection accepted.\n"
            << "[LB] Waiting for registration.\n";
    }

    return 0;
}

// Receives a bounded number of messages.
bool RegistrationServer::processConnection(
    Connection& connection
)
{
    connection.needsDrain = false;

    for (int i = 0;
        i < MAX_MESSAGES_PER_POLL;
        ++i)
    {
        int socketError = 0;

        const ReceiveStatus status =
            connection.receiver.receive(
                connection.socket,
                socketError
            );

        switch (status)
        {
        case ReceiveStatus::WouldBlock:
        {
            return true;
        }

        case ReceiveStatus::ConnectionClosed:
        {
            return false;
        }

        case ReceiveStatus::IncompleteMessage:
        {
            std::cout
                << "[LB] Connection closed with "
                << "an incomplete message.\n";

            return false;
        }

        case ReceiveStatus::InvalidMessage:
        {
            std::cout
                << "[LB] Invalid protocol message.\n";

            return false;
        }

        case ReceiveStatus::SocketError:
        {
            std::cout
                << "[LB] Socket error: "
                << socketError << "\n";

            return false;
        }

        case ReceiveStatus::MessageReady:
        {
            break;
        }

        default:
        {
            return false;
        }
        }

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        if (!connection.receiver.getMessage(
            header,
            payload))
        {
            return false;
        }

        bool accepted = false;

        if (connection.role ==
            ConnectionRole::Pending)
        {
            accepted = registerConnection(
                connection,
                header,
                payload
            );
        }
        else
        {
            accepted = processMessage(
                connection,
                header,
                payload
            );
        }

        if (!accepted)
        {
            return false;
        }

        if (!connection.receiver.consumeMessage())
        {
            return false;
        }
    }

    // More complete messages may already be buffered.
    connection.needsDrain = true;

    return true;
}

// Classifies and registers an accepted connection.
bool RegistrationServer::registerConnection(
    Connection& connection,
    const MessageHeader& header,
    const uint8_t* payload
)
{
    if (header.type == MessageType::REGISTER_UR)
    {
        RegisterUrPayload registration;

        if (!PayloadCodec::deserializeRegisterUr(
            payload,
            header.length,
            registration))
        {
            return false;
        }

        connection.role = ConnectionRole::User;

        std::cout
            << "[LB] User registered successfully.\n";

        return true;
    }

    if (header.type == MessageType::REGISTER_WR)
    {
        RegisterWrPayload registration;

        if (!PayloadCodec::deserializeRegisterWr(
            payload,
            header.length,
            registration))
        {
            return false;
        }

        if (registration.workerId == 0)
        {
            std::cout
                << "[LB] Invalid worker ID.\n";

            return false;
        }

        // Reject duplicate active worker identifiers.
        if (getWorkerSession(
            registration.workerId) != 0)
        {
            std::cout
                << "[LB] Duplicate worker ID rejected: "
                << registration.workerId << "\n";

            return false;
        }

        // Prevent session identifier reuse after overflow.
        if (nextSessionId_ == 0)
        {
            std::cout
                << "[LB] Session identifier limit reached.\n";

            return false;
        }

        connection.workerId = registration.workerId;

        connection.sessionId = nextSessionId_++;

        connection.role = ConnectionRole::Worker;

        std::cout
            << "[LB] Worker registered successfully."
            << " Worker ID: "
            << connection.workerId
            << " | Session ID: "
            << connection.sessionId << "\n";

        return true;
    }

    std::cout
        << "[LB] First message must be a registration.\n";

    return false;
}

// Handles messages according to the registered role.
bool RegistrationServer::processMessage(
    Connection& connection,
    const MessageHeader& header,
    const uint8_t* payload
)
{
    if (connection.role == ConnectionRole::User)
    {
        if (header.type != MessageType::REQUEST)
        {
            std::cout
                << "[LB] Unexpected message from user.\n";

            return false;
        }

        RequestPayload request;

        if (!PayloadCodec::deserializeRequest(
            payload,
            header.length,
            request))
        {
            return false;
        }

        ++observedRequestCount_;

        // Diagnostic output only.
        // Actual request registration is implemented later.
        std::cout
            << "[LB] REQUEST received."
            << " Client request ID: "
            << request.clientRequestId
            << " | Priority: "
            << static_cast<int>(request.priority)
            << " | Data size: "
            << request.dataSize
            << " bytes\n";

        std::cout
            << "[LB] Request observed, not queued yet.\n";

        return true;
    }

    if (connection.role == ConnectionRole::Worker)
    {
        if (header.type == MessageType::HEARTBEAT)
        {
            HeartbeatPayload heartbeat;

            if (!PayloadCodec::deserializeHeartbeat(
                payload,
                header.length,
                heartbeat))
            {
                return false;
            }

            std::cout
                << "[LB] HEARTBEAT received."
                << " Worker ID: "
                << connection.workerId << "\n";

            return true;
        }

        if (header.type == MessageType::RESULT)
        {
            ResultPayload result;

            if (!PayloadCodec::deserializeResult(
                payload,
                header.length,
                result))
            {
                return false;
            }

            // Diagnostic output only.
            std::cout
                << "[LB] RESULT received."
                << " Worker ID: "
                << connection.workerId
                << " | Request ID: "
                << result.lbRequestId
                << " | Attempt ID: "
                << result.attemptId << "\n";

            return true;
        }

        std::cout
            << "[LB] Unexpected message from worker.\n";

        return false;
    }

    return false;
}

// Releases a client connection.
int RegistrationServer::closeConnection(
    std::size_t index
) noexcept
{
    Connection* connection = connections_[index];

    if (connection == nullptr)
    {
        return 0;
    }

    const int result =
        SocketUtils::closeSocket(connection->socket);

    if (connection->role == ConnectionRole::User)
    {
        std::cout
            << "[LB] User disconnected.\n";
    }
    else if (connection->role ==
        ConnectionRole::Worker)
    {
        std::cout
            << "[LB] Worker disconnected."
            << " Worker ID: "
            << connection->workerId << "\n";
    }
    else
    {
        std::cout
            << "[LB] Unregistered connection closed.\n";
    }

    delete connection;

    connections_[index] = nullptr;

    return result;
}

// Finds an unused connection slot.
std::size_t RegistrationServer::findFreeSlot()
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

// Releases all network resources.
int RegistrationServer::stop() noexcept
{
    int firstError = 0;

    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        const int result = closeConnection(i);

        if (firstError == 0 && result != 0)
        {
            firstError = result;
        }
    }

    const int listenerResult = listener_.stop();

    if (firstError == 0)
    {
        firstError = listenerResult;
    }

    return firstError;
}

// Returns the server state.
bool RegistrationServer::isRunning() const noexcept
{
    return listener_.isListening();
}

// Returns the active local port.
uint16_t RegistrationServer::getPort() const noexcept
{
    return listener_.getPort();
}

// Counts all accepted connections.
std::size_t RegistrationServer::getConnectionCount()
const noexcept
{
    std::size_t count = 0;

    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        if (connections_[i] != nullptr)
        {
            ++count;
        }
    }

    return count;
}

// Counts registered users.
std::size_t RegistrationServer::getUserCount()
const noexcept
{
    std::size_t count = 0;

    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        if (connections_[i] != nullptr &&
            connections_[i]->role ==
            ConnectionRole::User)
        {
            ++count;
        }
    }

    return count;
}

// Counts registered workers.
std::size_t RegistrationServer::getWorkerCount()
const noexcept
{
    std::size_t count = 0;

    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        if (connections_[i] != nullptr &&
            connections_[i]->role ==
            ConnectionRole::Worker)
        {
            ++count;
        }
    }

    return count;
}

// Finds the active session of a registered worker.
uint64_t RegistrationServer::getWorkerSession(
    uint64_t workerId
) const noexcept
{
    for (std::size_t i = 0;
        i < MAX_CONNECTIONS;
        ++i)
    {
        const Connection* connection =
            connections_[i];

        if (connection != nullptr &&
            connection->role ==
            ConnectionRole::Worker &&
            connection->workerId == workerId)
        {
            return connection->sessionId;
        }
    }

    return 0;
}

// Returns the diagnostic request counter.
uint64_t RegistrationServer::getObservedRequestCount()
const noexcept
{
    return observedRequestCount_;
}