#pragma once

#include <cstdint>
#include "Enums.h"

// Represents a user request.
// The data buffer is owned by the caller.
struct Request
{
    uint64_t clientRequestId = 0;

    Priority priority = Priority::LOW;

    uint32_t dataSize = 0;

    const uint8_t* data = nullptr;
};