#pragma once

#include <winsock2.h>
#include <cstdint>
#include <cstddef>

#include "TcpListener.h"
#include "MessageIO.h"

// Manages connection registration during the
// single-threaded server development phase.
class RegistrationServer
{
public:
    static constexpr std::size_t MAX_CONNECTIONS =
        FD_SETSIZE - 1;

    explicit RegistrationServer(
        uint32_t registrationTimeoutMs = 5000
    ) noexcept;

    ~RegistrationServer();

    RegistrationServer(const RegistrationServer&) = delete;
    RegistrationServer& operator=(
        const RegistrationServer&) = delete;

    RegistrationServer(RegistrationServer&&) = delete;
    RegistrationServer& operator=(
        RegistrationServer&&) = delete;

    // Starts the registration server.
    int start(const char* ipAddress, uint16_t port);

    // Processes ready sockets and pending messages.
    int poll(uint32_t timeoutMs);

    // Releases all client sockets and the listener.
    int stop() noexcept;

    bool isRunning() const noexcept;

    uint16_t getPort() const noexcept;

    std::size_t getConnectionCount() const noexcept;

    std::size_t getUserCount() const noexcept;

    std::size_t getWorkerCount() const noexcept;

    // Returns zero if the worker is not registered.
    uint64_t getWorkerSession(
        uint64_t workerId
    ) const noexcept;

    // Returns the number of observed user requests.
    // Requests are not queued or dispatched in this phase.
    uint64_t getObservedRequestCount() const noexcept;

private:
    enum class ConnectionRole
    {
        Pending,
        User,
        Worker
    };

    // Owns one accepted socket and its receive state.
    struct Connection
    {
        SOCKET socket = INVALID_SOCKET;

        ConnectionRole role =
            ConnectionRole::Pending;

        uint64_t workerId = 0;
        uint64_t sessionId = 0;
        uint64_t acceptedAt = 0;

        bool needsDrain = false;

        MessageReceiver receiver;
    };

    TcpListener listener_;

    Connection* connections_[MAX_CONNECTIONS];

    uint32_t registrationTimeoutMs_;

    uint64_t nextSessionId_;

    uint64_t observedRequestCount_;

    // Accepts a bounded number of new connections.
    int acceptPending();

    // Receives messages from one connection.
    bool processConnection(Connection& connection);

    // Classifies the first message.
    bool registerConnection(
        Connection& connection,
        const MessageHeader& header,
        const uint8_t* payload
    );

    // Handles messages from registered connections.
    bool processMessage(
        Connection& connection,
        const MessageHeader& header,
        const uint8_t* payload
    );

    // Releases one connection.
    int closeConnection(std::size_t index) noexcept;

    // Finds an available connection slot.
    std::size_t findFreeSlot() const noexcept;
};