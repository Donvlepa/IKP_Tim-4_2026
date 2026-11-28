#include "pch.h"
#include "Protocol.h"
#include "Constants.h"

namespace Protocol
{
    // Writes a 16-bit integer in big-endian format.
    static void writeUint16(uint8_t* buffer, uint16_t value)
    {
        buffer[0] = static_cast<uint8_t>(value >> 8);
        buffer[1] = static_cast<uint8_t>(value);
    }

    // Writes a 32-bit integer in big-endian format.
    static void writeUint32(uint8_t* buffer, uint32_t value)
    {
        buffer[0] = static_cast<uint8_t>(value >> 24);
        buffer[1] = static_cast<uint8_t>(value >> 16);
        buffer[2] = static_cast<uint8_t>(value >> 8);
        buffer[3] = static_cast<uint8_t>(value);
    }

    // Reads a 16-bit integer from big-endian format.
    static uint16_t readUint16(const uint8_t* buffer)
    {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(buffer[0]) << 8) |
            static_cast<uint16_t>(buffer[1])
            );
    }

    // Reads a 32-bit integer from big-endian format.
    static uint32_t readUint32(const uint8_t* buffer)
    {
        return
            (static_cast<uint32_t>(buffer[0]) << 24) |
            (static_cast<uint32_t>(buffer[1]) << 16) |
            (static_cast<uint32_t>(buffer[2]) << 8) |
            static_cast<uint32_t>(buffer[3]);
    }

    // Checks whether a message type is supported.
    bool isValidMessageType(MessageType type)
    {
        switch (type)
        {
        case MessageType::REGISTER_UR:
        case MessageType::REGISTER_WR:
        case MessageType::REQUEST:
        case MessageType::ASSIGN:
        case MessageType::HEARTBEAT:
        case MessageType::RESULT:
        case MessageType::RESPONSE:
            return true;

        default:
            return false;
        }
    }

    // Validates all logical header fields.
    bool validateHeader(const MessageHeader& header)
    {
        if (header.length > Constants::MAX_PAYLOAD_SIZE)
        {
            return false;
        }

        if (!isValidMessageType(header.type))
        {
            return false;
        }

        if (header.version != Constants::PROTOCOL_VERSION)
        {
            return false;
        }

        return true;
    }

    // Serializes a header into network byte order.
    bool serializeHeader(
        const MessageHeader& header,
        uint8_t* buffer,
        std::size_t bufferSize)
    {
        if (buffer == nullptr ||
            bufferSize < Constants::HEADER_SIZE)
        {
            return false;
        }

        if (!validateHeader(header))
        {
            return false;
        }

        writeUint32(buffer, header.length);

        writeUint16(
            buffer + 4,
            static_cast<uint16_t>(header.type)
        );

        writeUint16(buffer + 6, header.version);

        return true;
    }

    // Deserializes a header from network byte order.
    bool deserializeHeader(
        const uint8_t* buffer,
        std::size_t bufferSize,
        MessageHeader& header)
    {
        if (buffer == nullptr ||
            bufferSize < Constants::HEADER_SIZE)
        {
            return false;
        }

        MessageHeader temporaryHeader;

        temporaryHeader.length = readUint32(buffer);

        temporaryHeader.type = static_cast<MessageType>(
            readUint16(buffer + 4)
            );

        temporaryHeader.version = readUint16(buffer + 6);

        if (!validateHeader(temporaryHeader))
        {
            return false;
        }

        header = temporaryHeader;

        return true;
    }
}