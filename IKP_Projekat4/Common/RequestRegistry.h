#pragma once

#include <winsock2.h>
#include <Windows.h>

#include <cstdint>
#include <cstddef>

#include "HashMap.h"
#include "Queue.h"
#include "RequestEntry.h"
#include "Payloads.h"

// Contains a safe copy of registered request metadata.
struct RegisteredRequestInfo
{
    uint64_t lbRequestId = 0;
    uint64_t clientRequestId = 0;

    uint64_t userSessionId = 0;

    SOCKET userSocket = INVALID_SOCKET;

    Priority priority = Priority::LOW;

    RequestStatus status = RequestStatus::QUEUED;

    uint64_t attemptId = 0;
    uint64_t assignedWorkerId = 0;

    bool userConnected = false;
    bool responseSent = false;

    uint32_t dataSize = 0;
};

// Owns request entries, priority queues and the request semaphore.
class RequestRegistry
{
public:
    static constexpr std::size_t MAX_REQUESTS = 20000;

    explicit RequestRegistry(
        std::size_t maxRequests = MAX_REQUESTS
    );

    ~RequestRegistry() noexcept;

    RequestRegistry(const RequestRegistry&) = delete;

    RequestRegistry& operator=(
        const RequestRegistry&) = delete;

    RequestRegistry(RequestRegistry&&) = delete;

    RequestRegistry& operator=(
        RequestRegistry&&) = delete;

    // Checks registry initialization and internal health.
    bool isValid() noexcept;

    // Registers a request and appends its ID to a priority queue.
    // Returns zero on success.
    int submit(
        SOCKET userSocket,
        uint64_t userSessionId,
        const RequestPayload& request,
        uint64_t& lbRequestId
    );

    // Returns a synchronized copy of request metadata.
    bool getRequest(
        uint64_t lbRequestId,
        RegisteredRequestInfo& information
    ) noexcept;

    // Copies request data into caller-owned memory.
    bool copyRequestData(
        uint64_t lbRequestId,
        uint8_t* output,
        std::size_t capacity,
        uint32_t& bytesCopied
    ) noexcept;

    // Marks requests belonging to a disconnected user session.
    std::size_t markUserDisconnected(
        uint64_t userSessionId
    ) noexcept;

    // Returns the first queued request without removing it.
    bool peekNext(
        Priority priority,
        uint64_t& lbRequestId
    ) noexcept;

    std::size_t size() noexcept;

    std::size_t getQueueSize(
        Priority priority
    ) noexcept;

    // Returns a borrowed semaphore handle.
    // The caller must not close the handle.
    HANDLE getRequestsSemaphore() const noexcept;

    // Returns a fatal internal synchronization error.
    int getFatalError() noexcept;

private:
    struct StoredRequest
    {
        RequestEntry entry;

        uint64_t userSessionId = 0;
    };

    HashMap<StoredRequest*> registry_;

    Queue<uint64_t> qh_;
    Queue<uint64_t> qm_;
    Queue<uint64_t> ql_;

    CRITICAL_SECTION lock_;

    HANDLE semRequests_;

    std::size_t maxRequests_;

    uint64_t nextRequestId_;

    int fatalError_;

    // Selects the queue associated with a priority.
    Queue<uint64_t>* selectQueue(
        Priority priority
    ) noexcept;
};