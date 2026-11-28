#pragma once

#include <winsock2.h>
#include <Windows.h>

#include <cstdint>
#include <cstddef>

#include "ConnectionContext.h"
#include "ConnectionHandoff.h"
#include "WorkerRegistry.h"

// Contains diagnostic information about an active worker.
struct TwrWorkerInfo
{
    uint64_t workerId = 0;
    uint64_t sessionId = 0;

    uint64_t lastHeartbeatAt = 0;

    uint64_t observedHeartbeats = 0;
    uint64_t observedResults = 0;

    uint64_t lastLbRequestId = 0;
    uint64_t lastAttemptId = 0;
};

// Contains diagnostic statistics of the TWR thread.
struct TwrStatistics
{
    std::size_t activeWorkers = 0;

    uint64_t registeredWorkers = 0;
    uint64_t rejectedWorkers = 0;
    uint64_t disconnectedWorkers = 0;

    uint64_t observedHeartbeats = 0;
    uint64_t observedResults = 0;

    int lastError = 0;
};

// Receives messages from worker connections.
class TwrReceiver
{
public:
    static constexpr std::size_t MAX_WORKERS =
        FD_SETSIZE - 1;

    // Diagnostic mode used by existing receiver tests.
    explicit TwrReceiver(
        ConnectionHandoff& handoff
    ) noexcept;

    // Production mode with a shared worker registry.
    TwrReceiver(
        ConnectionHandoff& handoff,
        WorkerRegistry& workerRegistry
    ) noexcept;

    ~TwrReceiver() noexcept;

    TwrReceiver(const TwrReceiver&) = delete;

    TwrReceiver& operator=(
        const TwrReceiver&) = delete;

    TwrReceiver(TwrReceiver&&) = delete;

    TwrReceiver& operator=(
        TwrReceiver&&) = delete;

    int start();

    int stop() noexcept;

    TwrStatistics getStatistics() noexcept;

    bool getWorker(
        uint64_t workerId,
        TwrWorkerInfo& information
    ) noexcept;

private:
    struct WorkerSlot
    {
        ConnectionContext* connection = nullptr;

        TwrWorkerInfo information;
    };

    ConnectionHandoff& handoff_;

    // Borrowed registry; must outlive the receiver.
    WorkerRegistry* workerRegistry_;

    WorkerSlot workers_[MAX_WORKERS];

    // Used only in diagnostic mode.
    uint64_t nextSessionId_;

    HANDLE stopEvent_;
    HANDLE thread_;

    TwrStatistics workingStatistics_;

    TwrStatistics publishedStatistics_;

    TwrWorkerInfo publishedWorkers_[MAX_WORKERS];

    CRITICAL_SECTION statisticsLock_;

    static unsigned __stdcall threadEntry(
        void* parameter
    );

    unsigned run();

    int pollOnce();

    void adoptConnections();

    void processWorker(std::size_t index);

    // Registers the worker and assigns its session.
    int registerWorkerConnection(
        ConnectionContext& connection,
        uint64_t heartbeatTimeMs
    );

    // Updates the worker registry and diagnostic state.
    bool handleHeartbeat(
        WorkerSlot& worker
    ) noexcept;

    void closeWorker(
        std::size_t index,
        bool rejected
    ) noexcept;

    void rejectConnection(
        ConnectionContext* connection
    ) noexcept;

    std::size_t findFreeSlot() const noexcept;

    bool workerExists(
        uint64_t workerId
    ) const noexcept;

    void publishStatistics() noexcept;

    bool shutdownRequested() const noexcept;
};