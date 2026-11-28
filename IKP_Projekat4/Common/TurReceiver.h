#pragma once

#include <winsock2.h>
#include <Windows.h>

#include <cstdint>
#include <cstddef>

#include "TcpListener.h"
#include "ConnectionContext.h"
#include "ConnectionHandoff.h"
#include "RequestRegistry.h"

// Contains diagnostic statistics of the TUR thread.
struct TurStatistics
{
    std::size_t pendingConnections = 0;
    std::size_t activeUsers = 0;

    uint64_t handedOffWorkers = 0;
    uint64_t observedRequests = 0;
    uint64_t rejectedConnections = 0;

    int lastError = 0;
};

// Accepts incoming connections and receives user requests.
class TurReceiver
{
public:
    static constexpr std::size_t MAX_CONNECTIONS =
        FD_SETSIZE - 1;

    // Diagnostic mode used by existing receiver tests.
    explicit TurReceiver(
        ConnectionHandoff& handoff,
        uint32_t registrationTimeoutMs = 5000
    ) noexcept;

    // Production mode with request registration.
    TurReceiver(
        ConnectionHandoff& handoff,
        RequestRegistry& requestRegistry,
        uint32_t registrationTimeoutMs = 5000
    ) noexcept;

    ~TurReceiver() noexcept;

    TurReceiver(const TurReceiver&) = delete;

    TurReceiver& operator=(
        const TurReceiver&) = delete;

    TurReceiver(TurReceiver&&) = delete;

    TurReceiver& operator=(
        TurReceiver&&) = delete;

    int start(const char* ipAddress, uint16_t port);

    int stop() noexcept;

    uint16_t getPort() const noexcept;

    TurStatistics getStatistics() noexcept;

private:
    ConnectionHandoff& handoff_;

    // Borrowed registry; must outlive the TUR receiver.
    RequestRegistry* requestRegistry_;

    TcpListener listener_;

    ConnectionContext* connections_[MAX_CONNECTIONS];

    uint32_t registrationTimeoutMs_;

    // User sessions are never reused during this object's lifetime.
    uint64_t nextUserSessionId_;

    HANDLE stopEvent_;
    HANDLE thread_;

    TurStatistics workingStatistics_;

    TurStatistics publishedStatistics_;

    CRITICAL_SECTION statisticsLock_;

    static unsigned __stdcall threadEntry(
        void* parameter
    );

    unsigned run();

    int pollOnce();

    int acceptPending();

    void processConnection(std::size_t index);

    // Registers and queues a decoded user request.
    int handleRequest(
        ConnectionContext& connection,
        const RequestPayload& request
    );

    void closeConnection(
        std::size_t index,
        bool rejected
    ) noexcept;

    std::size_t findFreeSlot() const noexcept;

    void publishStatistics() noexcept;

    bool shutdownRequested() const noexcept;
};