#pragma once

#include <cstdint>
#include "Enums.h"
#include "Request.h"

// Represents the final outcome of a request.
enum class ResponseStatus : uint8_t
{
    SUCCESS = 1,
    CANCELLED = 2
};

// REGISTER_UR contains no payload fields.
struct RegisterUrPayload
{
};

// REGISTER_WR payload.
struct RegisterWrPayload
{
    uint64_t workerId = 0;
};

// REQUEST payload.
// Reuses the existing user request model.
using RequestPayload = Request;

// ASSIGN payload.
// The data buffer is owned by the caller.
struct AssignPayload
{
    uint64_t lbRequestId = 0;
    uint64_t attemptId = 0;

    Priority priority = Priority::LOW;

    uint32_t dataSize = 0;

    const uint8_t* data = nullptr;
};

// HEARTBEAT contains no payload fields.
struct HeartbeatPayload
{
};

// RESULT payload.
// The result buffer is owned by the caller.
struct ResultPayload
{
    uint64_t lbRequestId = 0;
    uint64_t attemptId = 0;

    uint32_t resultSize = 0;

    const uint8_t* result = nullptr;
};

// RESPONSE payload.
// The result buffer is owned by the caller.
struct ResponsePayload
{
    uint64_t clientRequestId = 0;

    ResponseStatus status = ResponseStatus::SUCCESS;

    uint32_t resultSize = 0;

    const uint8_t* result = nullptr;
};