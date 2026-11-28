#pragma once

#include <winsock2.h>
#include <Windows.h>

#include <cstdint>
#include <cstddef>

#include "TcpListener.h"
#include "ConnectionContext.h"
#include "ConnectionHandoff.h"

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

// Accepts and classifies incoming TCP connections.
// Retains user connections and transfers worker connections.
class TurReceiver
{
public:
    static constexpr std::size_t MAX_CONNECTIONS =
        FD_SETSIZE - 1;

    explicit TurReceiver(
        ConnectionHandoff& handoff,
        uint32_t registrationTimeoutMs = 5000
    ) noexcept;

    ~TurReceiver() noexcept;

    TurReceiver(const TurReceiver&) = delete;

    TurReceiver& operator=(
        const TurReceiver&) = delete;

    TurReceiver(TurReceiver&&) = delete;

    TurReceiver& operator=(TurReceiver&&) = delete;

    // Starts the listener and the TUR thread.
    // Lifecycle methods must be called by the owner thread.
    int start(const char* ipAddress, uint16_t port);

    // Signals shutdown and waits for thread completion.
    int stop() noexcept;

    uint16_t getPort() const noexcept;

    // Returns a synchronized statistics snapshot.
    TurStatistics getStatistics() noexcept;

private:
    ConnectionHandoff& handoff_;

    TcpListener listener_;

    ConnectionContext* connections_[MAX_CONNECTIONS];

    uint32_t registrationTimeoutMs_;

    HANDLE stopEvent_;
    HANDLE thread_;

    // Modified exclusively by the TUR thread.
    TurStatistics workingStatistics_;

    // Shared snapshot protected by a critical section.
    TurStatistics publishedStatistics_;

    CRITICAL_SECTION statisticsLock_;

    // Windows thread entry point.
    static unsigned __stdcall threadEntry(
        void* parameter
    );

    // Executes the TUR event loop.
    unsigned run();

    // Processes one select iteration.
    int pollOnce();

    // Accepts incoming TCP connections.
    int acceptPending();

    // Processes one connection.
    void processConnection(std::size_t index);

    // Releases a locally owned connection.
    void closeConnection(
        std::size_t index,
        bool rejected
    ) noexcept;

    // Finds an available connection slot.
    std::size_t findFreeSlot() const noexcept;

    // Publishes diagnostic statistics.
    void publishStatistics() noexcept;

    bool shutdownRequested() const noexcept;
};