#include "pch.h"
#include "PayloadCodec.h"
#include "Constants.h"
#include <cstring>

namespace
{
    // Writes a 64-bit integer in network byte order.
    void writeUint64(uint8_t* buffer, uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
        {
            buffer[i] = static_cast<uint8_t>(
                value >> (56 - i * 8)
                );
        }
    }

    // Reads a 64-bit integer from network byte order.
    uint64_t readUint64(const uint8_t* buffer)
    {
        uint64_t value = 0;

        for (int i = 0; i < 8; ++i)
        {
            value = (value << 8) |
                static_cast<uint64_t>(buffer[i]);
        }

        return value;
    }

    // Checks whether a priority is supported.
    bool isValidPriority(Priority priority)
    {
        return priority == Priority::HIGH ||
            priority == Priority::MEDIUM ||
            priority == Priority::LOW;
    }

    // Checks whether a response status is supported.
    bool isValidResponseStatus(ResponseStatus status)
    {
        return status == ResponseStatus::SUCCESS ||
            status == ResponseStatus::CANCELLED;
    }

    // Validates serialization parameters.
    bool canSerialize(
        const uint8_t* data,
        uint32_t dataSize,
        uint32_t maxDataSize,
        std::size_t fixedSize,
        const uint8_t* buffer,
        std::size_t bufferSize
    )
    {
        if (buffer == nullptr)
        {
            return false;
        }

        if (dataSize > maxDataSize)
        {
            return false;
        }

        if (dataSize > 0 && data == nullptr)
        {
            return false;
        }

        const std::size_t totalSize =
            fixedSize + dataSize;

        if (totalSize > Constants::MAX_PAYLOAD_SIZE)
        {
            return false;
        }

        return bufferSize >= totalSize;
    }

    // Validates deserialization parameters.
    bool canDeserialize(
        const uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t fixedSize,
        uint32_t maxDataSize
    )
    {
        if (buffer == nullptr)
        {
            return false;
        }

        if (bufferSize < fixedSize ||
            bufferSize > Constants::MAX_PAYLOAD_SIZE)
        {
            return false;
        }

        return bufferSize - fixedSize <= maxDataSize;
    }

    // Copies application data into the output buffer.
    void copyData(
        uint8_t* destination,
        const uint8_t* source,
        uint32_t size
    )
    {
        if (size > 0)
        {
            std::memcpy(destination, source, size);
        }
    }
}

namespace PayloadCodec
{
    // REGISTER_UR

    bool serializeRegisterUr(
        const RegisterUrPayload&,
        uint8_t*,
        std::size_t,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;
        return true;
    }

    bool deserializeRegisterUr(
        const uint8_t*,
        std::size_t bufferSize,
        RegisterUrPayload&
    )
    {
        return bufferSize ==
            Constants::REGISTER_UR_PAYLOAD_SIZE;
    }

    // REGISTER_WR

    bool serializeRegisterWr(
        const RegisterWrPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;

        if (buffer == nullptr ||
            bufferSize < Constants::REGISTER_WR_PAYLOAD_SIZE)
        {
            return false;
        }

        writeUint64(buffer, payload.workerId);

        bytesWritten = Constants::REGISTER_WR_PAYLOAD_SIZE;

        return true;
    }

    bool deserializeRegisterWr(
        const uint8_t* buffer,
        std::size_t bufferSize,
        RegisterWrPayload& payload
    )
    {
        if (buffer == nullptr ||
            bufferSize != Constants::REGISTER_WR_PAYLOAD_SIZE)
        {
            return false;
        }

        payload.workerId = readUint64(buffer);

        return true;
    }

    // REQUEST

    bool serializeRequest(
        const RequestPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;

        if (!isValidPriority(payload.priority))
        {
            return false;
        }

        if (!canSerialize(
            payload.data,
            payload.dataSize,
            Constants::MAX_REQUEST_DATA_SIZE,
            Constants::REQUEST_FIXED_SIZE,
            buffer,
            bufferSize))
        {
            return false;
        }

        writeUint64(buffer, payload.clientRequestId);

        buffer[8] = static_cast<uint8_t>(payload.priority);

        copyData(
            buffer + Constants::REQUEST_FIXED_SIZE,
            payload.data,
            payload.dataSize
        );

        bytesWritten =
            Constants::REQUEST_FIXED_SIZE +
            payload.dataSize;

        return true;
    }

    bool deserializeRequest(
        const uint8_t* buffer,
        std::size_t bufferSize,
        RequestPayload& payload
    )
    {
        if (!canDeserialize(
            buffer,
            bufferSize,
            Constants::REQUEST_FIXED_SIZE,
            Constants::MAX_REQUEST_DATA_SIZE))
        {
            return false;
        }

        const Priority priority =
            static_cast<Priority>(buffer[8]);

        if (!isValidPriority(priority))
        {
            return false;
        }

        RequestPayload decoded;

        decoded.clientRequestId = readUint64(buffer);
        decoded.priority = priority;

        decoded.dataSize = static_cast<uint32_t>(
            bufferSize - Constants::REQUEST_FIXED_SIZE
            );

        decoded.data = decoded.dataSize > 0
            ? buffer + Constants::REQUEST_FIXED_SIZE
            : nullptr;

        payload = decoded;

        return true;
    }

    // ASSIGN

    bool serializeAssign(
        const AssignPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;

        if (!isValidPriority(payload.priority))
        {
            return false;
        }

        if (!canSerialize(
            payload.data,
            payload.dataSize,
            Constants::MAX_REQUEST_DATA_SIZE,
            Constants::ASSIGN_FIXED_SIZE,
            buffer,
            bufferSize))
        {
            return false;
        }

        writeUint64(buffer, payload.lbRequestId);
        writeUint64(buffer + 8, payload.attemptId);

        buffer[16] =
            static_cast<uint8_t>(payload.priority);

        copyData(
            buffer + Constants::ASSIGN_FIXED_SIZE,
            payload.data,
            payload.dataSize
        );

        bytesWritten =
            Constants::ASSIGN_FIXED_SIZE +
            payload.dataSize;

        return true;
    }

    bool deserializeAssign(
        const uint8_t* buffer,
        std::size_t bufferSize,
        AssignPayload& payload
    )
    {
        if (!canDeserialize(
            buffer,
            bufferSize,
            Constants::ASSIGN_FIXED_SIZE,
            Constants::MAX_REQUEST_DATA_SIZE))
        {
            return false;
        }

        const Priority priority =
            static_cast<Priority>(buffer[16]);

        if (!isValidPriority(priority))
        {
            return false;
        }

        AssignPayload decoded;

        decoded.lbRequestId = readUint64(buffer);
        decoded.attemptId = readUint64(buffer + 8);
        decoded.priority = priority;

        decoded.dataSize = static_cast<uint32_t>(
            bufferSize - Constants::ASSIGN_FIXED_SIZE
            );

        decoded.data = decoded.dataSize > 0
            ? buffer + Constants::ASSIGN_FIXED_SIZE
            : nullptr;

        payload = decoded;

        return true;
    }

    // HEARTBEAT

    bool serializeHeartbeat(
        const HeartbeatPayload&,
        uint8_t*,
        std::size_t,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;

        return true;
    }

    bool deserializeHeartbeat(
        const uint8_t*,
        std::size_t bufferSize,
        HeartbeatPayload&
    )
    {
        return bufferSize ==
            Constants::HEARTBEAT_PAYLOAD_SIZE;
    }

    // RESULT

    bool serializeResult(
        const ResultPayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;

        if (!canSerialize(
            payload.result,
            payload.resultSize,
            Constants::MAX_RESULT_DATA_SIZE,
            Constants::RESULT_FIXED_SIZE,
            buffer,
            bufferSize))
        {
            return false;
        }

        writeUint64(buffer, payload.lbRequestId);
        writeUint64(buffer + 8, payload.attemptId);

        copyData(
            buffer + Constants::RESULT_FIXED_SIZE,
            payload.result,
            payload.resultSize
        );

        bytesWritten =
            Constants::RESULT_FIXED_SIZE +
            payload.resultSize;

        return true;
    }

    bool deserializeResult(
        const uint8_t* buffer,
        std::size_t bufferSize,
        ResultPayload& payload
    )
    {
        if (!canDeserialize(
            buffer,
            bufferSize,
            Constants::RESULT_FIXED_SIZE,
            Constants::MAX_RESULT_DATA_SIZE))
        {
            return false;
        }

        ResultPayload decoded;

        decoded.lbRequestId = readUint64(buffer);
        decoded.attemptId = readUint64(buffer + 8);

        decoded.resultSize = static_cast<uint32_t>(
            bufferSize - Constants::RESULT_FIXED_SIZE
            );

        decoded.result = decoded.resultSize > 0
            ? buffer + Constants::RESULT_FIXED_SIZE
            : nullptr;

        payload = decoded;

        return true;
    }

    // RESPONSE

    bool serializeResponse(
        const ResponsePayload& payload,
        uint8_t* buffer,
        std::size_t bufferSize,
        std::size_t& bytesWritten
    )
    {
        bytesWritten = 0;

        if (!isValidResponseStatus(payload.status))
        {
            return false;
        }

        if (payload.status == ResponseStatus::CANCELLED &&
            payload.resultSize != 0)
        {
            return false;
        }

        if (!canSerialize(
            payload.result,
            payload.resultSize,
            Constants::MAX_RESULT_DATA_SIZE,
            Constants::RESPONSE_FIXED_SIZE,
            buffer,
            bufferSize))
        {
            return false;
        }

        writeUint64(buffer, payload.clientRequestId);

        buffer[8] =
            static_cast<uint8_t>(payload.status);

        copyData(
            buffer + Constants::RESPONSE_FIXED_SIZE,
            payload.result,
            payload.resultSize
        );

        bytesWritten =
            Constants::RESPONSE_FIXED_SIZE +
            payload.resultSize;

        return true;
    }

    bool deserializeResponse(
        const uint8_t* buffer,
        std::size_t bufferSize,
        ResponsePayload& payload
    )
    {
        if (!canDeserialize(
            buffer,
            bufferSize,
            Constants::RESPONSE_FIXED_SIZE,
            Constants::MAX_RESULT_DATA_SIZE))
        {
            return false;
        }

        const ResponseStatus status =
            static_cast<ResponseStatus>(buffer[8]);

        if (!isValidResponseStatus(status))
        {
            return false;
        }

        const uint32_t resultSize =
            static_cast<uint32_t>(
                bufferSize - Constants::RESPONSE_FIXED_SIZE
                );

        if (status == ResponseStatus::CANCELLED &&
            resultSize != 0)
        {
            return false;
        }

        ResponsePayload decoded;

        decoded.clientRequestId = readUint64(buffer);
        decoded.status = status;
        decoded.resultSize = resultSize;

        decoded.result = resultSize > 0
            ? buffer + Constants::RESPONSE_FIXED_SIZE
            : nullptr;

        payload = decoded;

        return true;
    }
}