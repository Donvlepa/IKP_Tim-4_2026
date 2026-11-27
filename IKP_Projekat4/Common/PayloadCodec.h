#pragma once

#include <cstdint>
#include <cstddef>
#include "Payloads.h"

namespace PayloadCodec
{
    // Serialization writes the payload into a caller-owned buffer.
    // bytesWritten is set to zero if serialization fails.
    // Source data must not overlap the destination buffer.

    // Deserialization returns views into the input buffer.
    // The input buffer must remain valid while these views are used.
    // The output payload remains unchanged on failure.

    bool serializeRegisterUr(
        const RegisterUrPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeRegisterUr(
        const uint8_t* buffer,
        std::size_t bufferSize,
        RegisterUrPayload& payload
    );

    bool serializeRegisterWr(
        const RegisterWrPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeRegisterWr(
        const uint8_t* buffer,
        std::size_t bufferSize,
        RegisterWrPayload& payload
    );

    bool serializeRequest(
        const RequestPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeRequest(
        const uint8_t* buffer,
        std::size_t bufferSize,
        RequestPayload& payload
    );

    bool serializeAssign(
        const AssignPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeAssign(
        const uint8_t* buffer,
        std::size_t bufferSize,
        AssignPayload& payload
    );

    bool serializeHeartbeat(
        const HeartbeatPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeHeartbeat(
        const uint8_t* buffer,
        std::size_t bufferSize,
        HeartbeatPayload& payload
    );

    bool serializeResult(
        const ResultPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeResult(
        const uint8_t* buffer,
        std::size_t bufferSize,
        ResultPayload& payload
    );

    bool serializeResponse(
        const ResponsePayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    );

    bool deserializeResponse(
        const uint8_t* buffer,
        std::size_t bufferSize,
        ResponsePayload& payload
    );
}