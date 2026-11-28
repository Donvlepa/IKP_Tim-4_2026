#include "pch.h"

#include <winsock2.h>
#include <Windows.h>

#include <new>

#include "WorkerRegistry.h"

namespace
{
    // Automatically releases the registry critical section.
    class WorkerRegistryLockGuard
    {
    public:
        explicit WorkerRegistryLockGuard(
            CRITICAL_SECTION& lock
        ) noexcept
            : lock_(lock)
        {
            EnterCriticalSection(&lock_);
        }

        ~WorkerRegistryLockGuard() noexcept
        {
            LeaveCriticalSection(&lock_);
        }

        WorkerRegistryLockGuard(
            const WorkerRegistryLockGuard&
        ) = delete;

        WorkerRegistryLockGuard& operator=(
            const WorkerRegistryLockGuard&
            ) = delete;

    private:
        CRITICAL_SECTION& lock_;
    };
}

// Initializes the worker registry and semaphore.
WorkerRegistry::WorkerRegistry(
    std::size_t maxWorkers,
    std::size_t maxQueueItems
)
    : registry_(16),
    semFreeWorkers_(nullptr),
    maxWorkers_(maxWorkers),
    maxQueueItems_(maxQueueItems),
    activeWorkers_(0),
    freeWorkerCount_(0),
    nextSessionId_(1),
    fatalError_(0)
{
    InitializeCriticalSection(&lock_);

    if (maxWorkers_ == 0 ||
        maxWorkers_ > MAX_WORKER_RECORDS ||
        maxQueueItems_ == 0 ||
        maxQueueItems_ > MAX_QUEUE_ITEMS)
    {
        return;
    }

    if (!registry_.isValid())
    {
        return;
    }

    semFreeWorkers_ = CreateSemaphoreW(
        nullptr,
        0,
        static_cast<LONG>(maxQueueItems_),
        nullptr
    );

    if (semFreeWorkers_ == nullptr)
    {
        fatalError_ =
            static_cast<int>(GetLastError());
    }
}

// Releases owned worker records and synchronization resources.
// All external threads must already be stopped.
WorkerRegistry::~WorkerRegistry() noexcept
{
    registry_.forEach(
        [](uint64_t, WorkerEntry*& worker)
        {
            delete worker;

            worker = nullptr;
        }
    );

    registry_.clear();

    freeWorkers_.clear();

    if (semFreeWorkers_ != nullptr)
    {
        CloseHandle(semFreeWorkers_);

        semFreeWorkers_ = nullptr;
    }

    DeleteCriticalSection(&lock_);
}

// Checks registry initialization and health.
bool WorkerRegistry::isValid() noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return registry_.isValid() &&
        semFreeWorkers_ != nullptr &&
        fatalError_ == 0;
}

// Registers a worker and queues its new session.
int WorkerRegistry::registerWorker(
    uint64_t workerId,
    SOCKET workerSocket,
    uint64_t heartbeatTimeMs,
    uint64_t& sessionId
)
{
    sessionId = 0;

    // STEP 1: Validate registration parameters.
    if (workerId == 0 ||
        workerSocket == INVALID_SOCKET)
    {
        return WSAEINVAL;
    }

    WorkerRegistryLockGuard guard(lock_);

    if (fatalError_ != 0)
    {
        return fatalError_;
    }

    if (!registry_.isValid() ||
        semFreeWorkers_ == nullptr)
    {
        return WSAEINVAL;
    }

    // STEP 2: Search for an existing worker record.
    WorkerEntry** found =
        registry_.find(workerId);

    WorkerEntry* worker =
        found != nullptr
        ? *found
        : nullptr;

    const bool isNewWorker =
        worker == nullptr;

    // An active worker identifier must be unique.
    if (!isNewWorker &&
        worker->status != WorkerStatus::DEAD)
    {
        return WSAEALREADY;
    }

    // STEP 3: Check capacity and identifier availability.
    if (nextSessionId_ == 0 ||
        freeWorkers_.size() >= maxQueueItems_)
    {
        return WSAENOBUFS;
    }

    if (isNewWorker &&
        registry_.size() >= maxWorkers_)
    {
        return WSAENOBUFS;
    }

    // STEP 4: Prepare the new session.
    const uint64_t newSessionId =
        nextSessionId_;

    WorkerEntry newState;

    newState.workerId = workerId;

    newState.sessionId = newSessionId;

    newState.workerSocket = workerSocket;

    newState.status = WorkerStatus::FREE;

    newState.lastHeartbeatMs = heartbeatTimeMs;

    newState.currentRequestId = 0;

    // Allocate a record only for a new worker identifier.
    if (isNewWorker)
    {
        worker = new (std::nothrow) WorkerEntry();

        if (worker == nullptr)
        {
            return WSAENOBUFS;
        }

        if (!registry_.insert(workerId, worker))
        {
            delete worker;

            return WSAENOBUFS;
        }
    }

    // STEP 5: Append the new session token to the free queue.
    FreeWorkerToken token;

    token.workerId = workerId;

    token.sessionId = newSessionId;

    if (!freeWorkers_.enqueue(token))
    {
        // Roll back a newly inserted worker record.
        if (isNewWorker)
        {
            registry_.remove(workerId);

            delete worker;
        }

        // An existing DEAD record remains unchanged.
        return WSAENOBUFS;
    }

    // STEP 6: Commit the new active worker session.
    *worker = newState;

    ++activeWorkers_;

    ++freeWorkerCount_;

    ++nextSessionId_;

    sessionId = newSessionId;

    // STEP 7: Notify the future dispatcher.
    if (!ReleaseSemaphore(
        semFreeWorkers_,
        1,
        nullptr))
    {
        // Preserve committed state for controlled shutdown.
        fatalError_ =
            static_cast<int>(GetLastError());

        return fatalError_;
    }

    return 0;
}

// Returns a synchronized worker record copy.
bool WorkerRegistry::getWorker(
    uint64_t workerId,
    WorkerEntry& information
) noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    WorkerEntry** found =
        registry_.find(workerId);

    if (found == nullptr ||
        *found == nullptr)
    {
        return false;
    }

    information = **found;

    return true;
}

// Updates heartbeat time for the matching active session.
bool WorkerRegistry::updateHeartbeat(
    uint64_t workerId,
    uint64_t sessionId,
    uint64_t heartbeatTimeMs
) noexcept
{
    if (workerId == 0 ||
        sessionId == 0)
    {
        return false;
    }

    WorkerRegistryLockGuard guard(lock_);

    WorkerEntry** found =
        registry_.find(workerId);

    if (found == nullptr ||
        *found == nullptr)
    {
        return false;
    }

    WorkerEntry& worker = **found;

    if (worker.sessionId != sessionId ||
        worker.status == WorkerStatus::DEAD)
    {
        return false;
    }

    // Never move the heartbeat timestamp backwards.
    if (heartbeatTimeMs > worker.lastHeartbeatMs)
    {
        worker.lastHeartbeatMs =
            heartbeatTimeMs;
    }

    return true;
}

// Marks the matching worker session as DEAD.
bool WorkerRegistry::markDead(
    uint64_t workerId,
    uint64_t sessionId,
    WorkerEntry& previousState
) noexcept
{
    if (workerId == 0 ||
        sessionId == 0)
    {
        return false;
    }

    WorkerRegistryLockGuard guard(lock_);

    WorkerEntry** found =
        registry_.find(workerId);

    if (found == nullptr ||
        *found == nullptr)
    {
        return false;
    }

    WorkerEntry& worker = **found;

    // Ignore stale disconnection events.
    if (worker.sessionId != sessionId ||
        worker.status == WorkerStatus::DEAD)
    {
        return false;
    }

    // Preserve the previous assignment for future recovery.
    previousState = worker;

    if (worker.status == WorkerStatus::FREE)
    {
        --freeWorkerCount_;
    }

    --activeWorkers_;

    worker.status = WorkerStatus::DEAD;

    worker.workerSocket = INVALID_SOCKET;

    worker.currentRequestId = 0;

    // Do not remove the old queue token here.
    // The dispatcher will discard it after consuming
    // its corresponding semaphore signal.

    return true;
}

// Returns the first physical queue token.
bool WorkerRegistry::peekFreeWorker(
    FreeWorkerToken& token
) noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return freeWorkers_.peek(token);
}

// Validates a free worker queue token.
bool WorkerRegistry::isFreeWorker(
    const FreeWorkerToken& token
) noexcept
{
    if (token.workerId == 0 ||
        token.sessionId == 0)
    {
        return false;
    }

    WorkerRegistryLockGuard guard(lock_);

    WorkerEntry** found =
        registry_.find(token.workerId);

    if (found == nullptr ||
        *found == nullptr)
    {
        return false;
    }

    const WorkerEntry& worker = **found;

    return worker.sessionId == token.sessionId &&
        worker.status == WorkerStatus::FREE &&
        worker.workerSocket != INVALID_SOCKET;
}

// Returns the number of worker records.
std::size_t WorkerRegistry::size() noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return registry_.size();
}

// Returns the number of active workers.
std::size_t WorkerRegistry::getActiveWorkerCount()
noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return activeWorkers_;
}

// Returns the number of currently free workers.
std::size_t WorkerRegistry::getFreeWorkerCount()
noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return freeWorkerCount_;
}

// Returns the physical queue size.
std::size_t WorkerRegistry::getFreeQueueSize()
noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return freeWorkers_.size();
}

// Returns the borrowed free worker semaphore.
HANDLE WorkerRegistry::getFreeWorkersSemaphore()
const noexcept
{
    return semFreeWorkers_;
}

// Returns a fatal internal synchronization error.
int WorkerRegistry::getFatalError() noexcept
{
    WorkerRegistryLockGuard guard(lock_);

    return fatalError_;
}