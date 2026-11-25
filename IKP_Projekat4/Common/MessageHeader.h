#pragma once

#include <cstdint>
#include "Enums.h"
#include "Constants.h"

// Represents the logical header of a protocol message.
struct MessageHeader
{
    // Payload length in bytes.
    uint32_t length = 0;

    // Communication message type.
    MessageType type = MessageType::REQUEST;

    // Communication protocol version.
    uint16_t version = Constants::PROTOCOL_VERSION;
};