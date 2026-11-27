#pragma once

#include <cstdint>

// Defines request priority levels.
enum class Priority : uint8_t
{
    HIGH = 1,
    MEDIUM = 2,
    LOW = 3
};

// Defines the current state of a request.
enum class RequestStatus : uint8_t
{
    QUEUED = 1,
    IN_PROGRESS = 2,
    COMPLETED = 3,
    CANCELLED = 4
};

// Defines the current state of a worker.
enum class WorkerStatus : uint8_t
{
    FREE = 1,
    BUSY = 2,
    DEAD = 3
};

// Defines supported communication message types.
enum class MessageType : uint16_t
{
    REGISTER_UR = 1,
    REGISTER_WR = 2,
    REQUEST = 3,
    ASSIGN = 4,
    HEARTBEAT = 5,
    RESULT = 6,
    RESPONSE = 7
};