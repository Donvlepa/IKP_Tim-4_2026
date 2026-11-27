#include "pch.h"

#include "MessageBuffer.h"
#include "Protocol.h"
#include "PayloadCodec.h"

#include <cstring>

MessageBuffer::MessageBuffer()
{
    reset();
}

// Appends incoming bytes until a complete message is received.
MessageBufferStatus MessageBuffer::append(
    const uint8_t* data,
    std::size_t dataSize,
    std::size_t& bytesConsumed
)
{
    bytesConsumed = 0;

    if (error_)
    {
        return MessageBufferStatus::InvalidMessage;
    }

    if (data == nullptr && dataSize > 0)
    {
        return MessageBufferStatus::InvalidInput;
    }

    if (messageReady_)
    {
        return MessageBufferStatus::MessageReady;
    }

    // Receive the fixed-size header first.
    if (!headerParsed_)
    {
        const std::size_t missingHeaderBytes =
            Constants::HEADER_SIZE - bufferedSize_;

        const std::size_t bytesToCopy =
            dataSize < missingHeaderBytes
            ? dataSize
            : missingHeaderBytes;

        if (bytesToCopy > 0)
        {
            std::memcpy(
                buffer_ + bufferedSize_,
                data,
                bytesToCopy
            );

            bufferedSize_ += bytesToCopy;
            bytesConsumed += bytesToCopy;
        }

        // Wait until the complete header is available.
        if (bufferedSize_ < Constants::HEADER_SIZE)
        {
            return MessageBufferStatus::NeedMoreData;
        }

        // Decode and validate the received header.
        if (!Protocol::deserializeHeader(
            buffer_,
            Constants::HEADER_SIZE,
            header_))
        {
            error_ = true;

            return MessageBufferStatus::InvalidMessage;
        }

        if (!Protocol::validateHeader(header_))
        {
            error_ = true;

            return MessageBufferStatus::InvalidMessage;
        }

        // Reject invalid message-specific payload lengths.
        if (!validatePayloadLength())
        {
            error_ = true;

            return MessageBufferStatus::InvalidMessage;
        }

        headerParsed_ = true;

        expectedSize_ =
            Constants::HEADER_SIZE + header_.length;
    }

    // Receive the remaining payload bytes.
    const std::size_t remainingInput =
        dataSize - bytesConsumed;

    const std::size_t missingMessageBytes =
        expectedSize_ - bufferedSize_;

    const std::size_t bytesToCopy =
        remainingInput < missingMessageBytes
        ? remainingInput
        : missingMessageBytes;

    if (bytesToCopy > 0)
    {
        std::memcpy(
            buffer_ + bufferedSize_,
            data + bytesConsumed,
            bytesToCopy
        );

        bufferedSize_ += bytesToCopy;
        bytesConsumed += bytesToCopy;
    }

    // The message is not complete yet.
    if (bufferedSize_ < expectedSize_)
    {
        return MessageBufferStatus::NeedMoreData;
    }

    // Validate the complete message payload.
    if (!validatePayload())
    {
        error_ = true;

        return MessageBufferStatus::InvalidMessage;
    }

    messageReady_ = true;

    return MessageBufferStatus::MessageReady;
}

// Checks whether a complete message is available.
bool MessageBuffer::hasMessage() const
{
    return messageReady_;
}

// Checks whether the buffer is in an error state.
bool MessageBuffer::hasError() const
{
    return error_;
}

// Returns the number of stored bytes.
std::size_t MessageBuffer::getBufferedSize() const
{
    return bufferedSize_;
}

// Returns the current message without removing it.
bool MessageBuffer::getMessage(
    MessageHeader& header,
    const uint8_t*& payload
) const
{
    if (!messageReady_ || error_)
    {
        return false;
    }

    header = header_;

    payload = header_.length > 0
        ? buffer_ + Constants::HEADER_SIZE
        : nullptr;

    return true;
}

// Removes the currently available message.
bool MessageBuffer::consumeMessage()
{
    if (!messageReady_ || error_)
    {
        return false;
    }

    reset();

    return true;
}

// Resets the buffer to its initial state.
void MessageBuffer::reset()
{
    bufferedSize_ = 0;
    expectedSize_ = Constants::HEADER_SIZE;

    header_ = MessageHeader{};

    headerParsed_ = false;
    messageReady_ = false;
    error_ = false;
}

// Checks payload length constraints for each message type.
bool MessageBuffer::validatePayloadLength() const
{
    const uint32_t length = header_.length;

    switch (header_.type)
    {
    case MessageType::REGISTER_UR:

        return length ==
            Constants::REGISTER_UR_PAYLOAD_SIZE;

    case MessageType::REGISTER_WR:

        return length ==
            Constants::REGISTER_WR_PAYLOAD_SIZE;

    case MessageType::REQUEST:

        return length >=
            Constants::REQUEST_FIXED_SIZE &&

            length <=
            Constants::REQUEST_FIXED_SIZE +
            Constants::MAX_REQUEST_DATA_SIZE;

    case MessageType::ASSIGN:

        return length >=
            Constants::ASSIGN_FIXED_SIZE &&

            length <=
            Constants::ASSIGN_FIXED_SIZE +
            Constants::MAX_REQUEST_DATA_SIZE;

    case MessageType::HEARTBEAT:

        return length ==
            Constants::HEARTBEAT_PAYLOAD_SIZE;

    case MessageType::RESULT:

        return length >=
            Constants::RESULT_FIXED_SIZE &&

            length <=
            Constants::RESULT_FIXED_SIZE +
            Constants::MAX_RESULT_DATA_SIZE;

    case MessageType::RESPONSE:

        return length >=
            Constants::RESPONSE_FIXED_SIZE &&

            length <=
            Constants::RESPONSE_FIXED_SIZE +
            Constants::MAX_RESULT_DATA_SIZE;

    default:

        return false;
    }
}

// Validates the payload according to its message type.
bool MessageBuffer::validatePayload() const
{
    const uint8_t* payload =
        buffer_ + Constants::HEADER_SIZE;

    const std::size_t payloadSize =
        header_.length;

    switch (header_.type)
    {
    case MessageType::REGISTER_UR:
    {
        RegisterUrPayload decoded;

        return PayloadCodec::deserializeRegisterUr(
            payload,
            payloadSize,
            decoded
        );
    }

    case MessageType::REGISTER_WR:
    {
        RegisterWrPayload decoded;

        return PayloadCodec::deserializeRegisterWr(
            payload,
            payloadSize,
            decoded
        );
    }

    case MessageType::REQUEST:
    {
        RequestPayload decoded;

        return PayloadCodec::deserializeRequest(
            payload,
            payloadSize,
            decoded
        );
    }

    case MessageType::ASSIGN:
    {
        AssignPayload decoded;

        return PayloadCodec::deserializeAssign(
            payload,
            payloadSize,
            decoded
        );
    }

    case MessageType::HEARTBEAT:
    {
        HeartbeatPayload decoded;

        return PayloadCodec::deserializeHeartbeat(
            payload,
            payloadSize,
            decoded
        );
    }

    case MessageType::RESULT:
    {
        ResultPayload decoded;

        return PayloadCodec::deserializeResult(
            payload,
            payloadSize,
            decoded
        );
    }

    case MessageType::RESPONSE:
    {
        ResponsePayload decoded;

        return PayloadCodec::deserializeResponse(
            payload,
            payloadSize,
            decoded
        );
    }

    default:

        return false;
    }
}