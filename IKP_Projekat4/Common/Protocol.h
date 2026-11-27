#pragma once

#include <cstdint>
#include <cstddef>
#include "MessageHeader.h"

namespace Protocol
{
    // Checks whether a message type is supported.
    bool isValidMessageType(MessageType type);

    // Validates all logical header fields.
    bool validateHeader(const MessageHeader& header);

    // Serializes a header into network byte order.
    bool serializeHeader(
        const MessageHeader& header,
        uint8_t* buffer,
        std::size_t bufferSize
    );

    // Deserializes a header from network byte order.
    bool deserializeHeader(
        const uint8_t* buffer,
        std::size_t bufferSize,
        MessageHeader& header
    );
}