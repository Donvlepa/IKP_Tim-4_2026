#pragma once

#include <cstdint>
#include <cstddef>

#include "Constants.h"
#include "MessageHeader.h"

// Represents the current result of message reception.
enum class MessageBufferStatus
{
    NeedMoreData,
    MessageReady,
    InvalidMessage,
    InvalidInput
};

// Accumulates TCP data and extracts complete protocol messages.
class MessageBuffer
{
public:
    MessageBuffer();

    MessageBuffer(const MessageBuffer&) = delete;
    MessageBuffer& operator=(const MessageBuffer&) = delete;

    // Appends data until one complete message is available.
    // bytesConsumed contains the number of accepted input bytes.
    MessageBufferStatus append(
        const uint8_t* data,
        std::size_t dataSize,
        std::size_t& bytesConsumed
    );

    // Checks whether a complete valid message is available.
    bool hasMessage() const;

    // Checks whether an invalid message has been detected.
    bool hasError() const;

    // Returns the number of currently buffered bytes.
    std::size_t getBufferedSize() const;

    // Returns the decoded header and a view of the payload.
    // The payload pointer is valid until consumeMessage(),
    // reset(), or destruction of this object.
    bool getMessage(
        MessageHeader& header,
        const uint8_t*& payload
    ) const;

    // Removes the current complete message.
    bool consumeMessage();

    // Clears all reception state, including protocol errors.
    void reset();

private:
    uint8_t buffer_[Constants::MAX_MESSAGE_SIZE];

    std::size_t bufferedSize_;
    std::size_t expectedSize_;

    MessageHeader header_;

    bool headerParsed_;
    bool messageReady_;
    bool error_;

    // Checks message-specific payload length constraints.
    bool validatePayloadLength() const;

    // Validates the complete payload using PayloadCodec.
    bool validatePayload() const;
};