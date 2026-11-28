#include "pch.h"

#include <winsock2.h>
#include <Windows.h>

#include <new>
#include <cstring>

#include "RequestRegistry.h"
#include "Constants.h"

namespace
{
    // Automatically releases a critical section.
    class RegistryLockGuard
    {
    public:
        explicit RegistryLockGuard(
            CRITICAL_SECTION& lock
        ) noexcept
            : lock_(lock)
        {
            EnterCriticalSection(&lock_);
        }

        ~RegistryLockGuard() noexcept
        {
            LeaveCriticalSection(&lock_);
        }

        RegistryLockGuard(
            const RegistryLockGuard&
        ) = delete;

        RegistryLockGuard& operator=(
            const RegistryLockGuard&
            ) = delete;

    private:
        CRITICAL_SECTION& lock_;
    };
}

// Initializes the request registry and semaphore.
RequestRegistry::RequestRegistry(
    std::size_t maxRequests
)
    : registry_(16),
    semRequests_(nullptr),
    maxRequests_(maxRequests),
    nextRequestId_(1),
    fatalError_(0)
{
    InitializeCriticalSection(&lock_);

    if (maxRequests_ == 0 ||
        maxRequests_ > MAX_REQUESTS)
    {
        return;
    }

    if (!registry_.isValid())
    {
        return;
    }

    semRequests_ = CreateSemaphoreW(
        nullptr,
        0,
        static_cast<LONG>(maxRequests_),
        nullptr
    );

    if (semRequests_ == nullptr)
    {
        fatalError_ =
            static_cast<int>(GetLastError());
    }
}

// Releases all owned requests and synchronization resources.
// All external threads must already be stopped.
RequestRegistry::~RequestRegistry() noexcept
{
    registry_.forEach(
        [](uint64_t, StoredRequest*& request)
        {
            delete request;

            request = nullptr;
        }
    );

    registry_.clear();

    qh_.clear();
    qm_.clear();
    ql_.clear();

    if (semRequests_ != nullptr)
    {
        CloseHandle(semRequests_);

        semRequests_ = nullptr;
    }

    DeleteCriticalSection(&lock_);
}

// Checks registry initialization and health.
bool RequestRegistry::isValid() noexcept
{
    RegistryLockGuard guard(lock_);

    return registry_.isValid() &&
        semRequests_ != nullptr &&
        fatalError_ == 0;
}

// Selects the corresponding priority queue.
Queue<uint64_t>* RequestRegistry::selectQueue(
    Priority priority
) noexcept
{
    switch (priority)
    {
    case Priority::HIGH:
        return &qh_;

    case Priority::MEDIUM:
        return &qm_;

    case Priority::LOW:
        return &ql_;

    default:
        return nullptr;
    }
}

// Registers and queues a new request.
int RequestRegistry::submit(
    SOCKET userSocket,
    uint64_t userSessionId,
    const RequestPayload& request,
    uint64_t& lbRequestId
)
{
    lbRequestId = 0;

    // STEP 1: Validate request parameters.
    if (userSocket == INVALID_SOCKET ||
        userSessionId == 0 ||
        request.dataSize >
        Constants::MAX_REQUEST_DATA_SIZE ||
        (request.dataSize > 0 &&
            request.data == nullptr))
    {
        return WSAEINVAL;
    }

    Queue<uint64_t>* queue =
        selectQueue(request.priority);

    if (queue == nullptr)
    {
        return WSAEINVAL;
    }

    // Protect the complete registration operation.
    RegistryLockGuard guard(lock_);

    if (fatalError_ != 0)
    {
        return fatalError_;
    }

    if (!registry_.isValid() ||
        semRequests_ == nullptr)
    {
        return WSAEINVAL;
    }

    // Prevent capacity and identifier overflow.
    if (registry_.size() >= maxRequests_ ||
        nextRequestId_ == 0)
    {
        return WSAENOBUFS;
    }

    // STEP 2: Allocate the owned request entry.
    StoredRequest* stored =
        new (std::nothrow) StoredRequest();

    if (stored == nullptr)
    {
        return WSAENOBUFS;
    }

    const uint64_t newId =
        nextRequestId_;

    stored->userSessionId =
        userSessionId;

    RequestEntry& entry = stored->entry;

    entry.lbRequestId = newId;

    entry.clientRequestId =
        request.clientRequestId;

    entry.userSocket = userSocket;

    entry.priority = request.priority;

    entry.status = RequestStatus::QUEUED;

    entry.attemptId = 0;

    entry.assignedWorkerId = 0;

    entry.userConnected = true;

    entry.responseSent = false;

    // STEP 3: Create an owned copy of the request data.
    if (!entry.setData(
        request.data,
        request.dataSize))
    {
        delete stored;

        return WSAENOBUFS;
    }

    // STEP 4: Insert the entry into the registry.
    if (!registry_.insert(newId, stored))
    {
        delete stored;

        return WSAENOBUFS;
    }

    // STEP 5: Append the request identifier to its queue.
    if (!queue->enqueue(newId))
    {
        registry_.remove(newId);

        delete stored;

        return WSAENOBUFS;
    }

    // The entry and its queue item are now committed.
    lbRequestId = newId;

    ++nextRequestId_;

    // STEP 6: Notify the future dispatcher.
    if (!ReleaseSemaphore(
        semRequests_,
        1,
        nullptr))
    {
        // Preserve the committed request for controlled shutdown.
        // Do not accept further requests after an internal failure.
        fatalError_ =
            static_cast<int>(GetLastError());

        return fatalError_;
    }

    return 0;
}

// Returns a safe copy of request metadata.
bool RequestRegistry::getRequest(
    uint64_t lbRequestId,
    RegisteredRequestInfo& information
) noexcept
{
    RegistryLockGuard guard(lock_);

    StoredRequest** found =
        registry_.find(lbRequestId);

    if (found == nullptr ||
        *found == nullptr)
    {
        return false;
    }

    const StoredRequest& stored =
        **found;

    const RequestEntry& entry =
        stored.entry;

    information.lbRequestId =
        entry.lbRequestId;

    information.clientRequestId =
        entry.clientRequestId;

    information.userSessionId =
        stored.userSessionId;

    information.userSocket =
        entry.userSocket;

    information.priority =
        entry.priority;

    information.status =
        entry.status;

    information.attemptId =
        entry.attemptId;

    information.assignedWorkerId =
        entry.assignedWorkerId;

    information.userConnected =
        entry.userConnected;

    information.responseSent =
        entry.responseSent;

    information.dataSize =
        entry.getDataSize();

    return true;
}

// Copies request data into caller-owned memory.
bool RequestRegistry::copyRequestData(
    uint64_t lbRequestId,
    uint8_t* output,
    std::size_t capacity,
    uint32_t& bytesCopied
) noexcept
{
    bytesCopied = 0;

    RegistryLockGuard guard(lock_);

    StoredRequest** found =
        registry_.find(lbRequestId);

    if (found == nullptr ||
        *found == nullptr)
    {
        return false;
    }

    const RequestEntry& entry =
        (*found)->entry;

    const uint32_t dataSize =
        entry.getDataSize();

    if (capacity < dataSize ||
        (dataSize > 0 && output == nullptr))
    {
        return false;
    }

    if (dataSize > 0)
    {
        std::memcpy(
            output,
            entry.getData(),
            dataSize
        );
    }

    bytesCopied = dataSize;

    return true;
}

// Marks requests belonging to a disconnected user session.
std::size_t RequestRegistry::markUserDisconnected(
    uint64_t userSessionId
) noexcept
{
    if (userSessionId == 0)
    {
        return 0;
    }

    RegistryLockGuard guard(lock_);

    std::size_t updated = 0;

    registry_.forEach(
        [&](uint64_t, StoredRequest*& stored)
        {
            if (stored != nullptr &&
                stored->userSessionId == userSessionId &&
                stored->entry.userConnected)
            {
                stored->entry.userConnected = false;

                ++updated;
            }
        }
    );

    return updated;
}

// Returns the first request ID without removing it.
bool RequestRegistry::peekNext(
    Priority priority,
    uint64_t& lbRequestId
) noexcept
{
    RegistryLockGuard guard(lock_);

    Queue<uint64_t>* queue =
        selectQueue(priority);

    if (queue == nullptr)
    {
        return false;
    }

    return queue->peek(lbRequestId);
}

// Returns the number of registered requests.
std::size_t RequestRegistry::size() noexcept
{
    RegistryLockGuard guard(lock_);

    return registry_.size();
}

// Returns the number of requests in one priority queue.
std::size_t RequestRegistry::getQueueSize(
    Priority priority
) noexcept
{
    RegistryLockGuard guard(lock_);

    Queue<uint64_t>* queue =
        selectQueue(priority);

    return queue != nullptr
        ? queue->size()
        : 0;
}

// Returns the borrowed request semaphore.
HANDLE RequestRegistry::getRequestsSemaphore()
const noexcept
{
    return semRequests_;
}

// Returns a fatal synchronization error.
int RequestRegistry::getFatalError() noexcept
{
    RegistryLockGuard guard(lock_);

    return fatalError_;
}