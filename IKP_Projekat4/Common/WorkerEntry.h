#pragma once

#include <winsock2.h>
#include <cstdint>
#include "Enums.h"

// Represents a worker registered in the Load Balancer.
struct WorkerEntry
{
    // Unique worker identifier.
    uint64_t workerId = 0;

    // Unique connection session identifier.
    uint64_t sessionId = 0;

    // Worker TCP socket.
    SOCKET workerSocket = INVALID_SOCKET;

    // Current worker status.
    WorkerStatus status = WorkerStatus::DEAD;

    // Timestamp of the last heartbeat in milliseconds.
    uint64_t lastHeartbeatMs = 0;

    // Identifier of the currently assigned request.
    uint64_t currentRequestId = 0;
};