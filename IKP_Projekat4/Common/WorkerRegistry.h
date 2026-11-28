#pragma once

#include <winsock2.h>
#include <Windows.h>

#include <cstdint>
#include <cstddef>

#include "WorkerEntry.h"
#include "HashMap.h"
#include "Queue.h"

// Identifies a specific worker session in the free worker queue.
struct FreeWorkerToken
{
    uint64_t workerId = 0;
    uint64_t sessionId = 0;
};

// Owns worker records, the free worker queue and its semaphore.
// Does not own or close worker sockets.
class WorkerRegistry
{
public:
    static constexpr std::size_t MAX_WORKER_RECORDS = 20000;
    static constexpr std::size_t MAX_QUEUE_ITEMS = 20000;

    explicit WorkerRegistry(
        std::size_t maxWorkers = MAX_WORKER_RECORDS,
        std::size_t maxQueueItems = MAX_QUEUE_ITEMS
    );

    ~WorkerRegistry() noexcept;

    WorkerRegistry(const WorkerRegistry&) = delete;

    WorkerRegistry& operator=(
        const WorkerRegistry&) = delete;

    WorkerRegistry(WorkerRegistry&&) = delete;

    WorkerRegistry& operator=(
        WorkerRegistry&&) = delete;

    // Checks registry initialization and internal health.
    bool isValid() noexcept;

    // Registers a new worker session and adds a free worker token.
    // Returns zero on success or an error code.
    int registerWorker(
        uint64_t workerId,
        SOCKET workerSocket,
        uint64_t heartbeatTimeMs,
        uint64_t& sessionId
    );

    // Returns a synchronized worker record copy.
    bool getWorker(
        uint64_t workerId,
        WorkerEntry& information
    ) noexcept;

    // Updates heartbeat time only for the active session.
    bool updateHeartbeat(
        uint64_t workerId,
        uint64_t sessionId,
        uint64_t heartbeatTimeMs
    ) noexcept;

    // Marks the matching active session as DEAD.
    // Returns its previous state for future recovery processing.
    bool markDead(
        uint64_t workerId,
        uint64_t sessionId,
        WorkerEntry& previousState
    ) noexcept;

    // Returns the first physical queue item without removing it.
    // The returned token may represent a stale worker session.
    bool peekFreeWorker(
        FreeWorkerToken& token
    ) noexcept;

    // Checks whether a token identifies a currently free worker.
    bool isFreeWorker(
        const FreeWorkerToken& token
    ) noexcept;

    // Returns the total number of records, including DEAD workers.
    std::size_t size() noexcept;

    std::size_t getActiveWorkerCount() noexcept;

    std::size_t getFreeWorkerCount() noexcept;

    // Returns the physical queue size, including stale tokens.
    std::size_t getFreeQueueSize() noexcept;

    // Returns a borrowed semaphore handle.
    HANDLE getFreeWorkersSemaphore() const noexcept;

    int getFatalError() noexcept;

private:
    HashMap<WorkerEntry*> registry_;

    Queue<FreeWorkerToken> freeWorkers_;

    CRITICAL_SECTION lock_;

    HANDLE semFreeWorkers_;

    std::size_t maxWorkers_;
    std::size_t maxQueueItems_;

    std::size_t activeWorkers_;
    std::size_t freeWorkerCount_;

    uint64_t nextSessionId_;

    int fatalError_;
};