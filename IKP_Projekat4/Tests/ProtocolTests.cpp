#include <iostream>
#include <cstdlib>
#include <cstdint>
#include "Protocol.h"
#include "Constants.h"
#include "TestSuites.h"

// Checks a test condition.
static void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";
        std::abort();
    }
}

// Tests all supported message types.
static void testValidMessageTypes()
{
    for (uint16_t i = 1; i <= 7; i++)
    {
        check(
            Protocol::isValidMessageType(
                static_cast<MessageType>(i)
            ),
            "Valid message type rejected"
        );
    }

    std::cout << "[PASS] Valid message types test\n";
}

// Tests rejection of unsupported message types.
static void testInvalidMessageTypes()
{
    check(
        !Protocol::isValidMessageType(
            static_cast<MessageType>(0)
        ),
        "Message type 0 accepted"
    );

    check(
        !Protocol::isValidMessageType(
            static_cast<MessageType>(8)
        ),
        "Message type 8 accepted"
    );

    check(
        !Protocol::isValidMessageType(
            static_cast<MessageType>(65535)
        ),
        "Unknown message type accepted"
    );

    std::cout << "[PASS] Invalid message types test\n";
}

// Tests header validation.
static void testHeaderValidation()
{
    MessageHeader header;

    header.length = 100;
    header.type = MessageType::REQUEST;

    check(
        Protocol::validateHeader(header),
        "Valid header rejected"
    );

    header.length = Constants::MAX_PAYLOAD_SIZE + 1;

    check(
        !Protocol::validateHeader(header),
        "Invalid payload length accepted"
    );

    header.length = 100;
    header.version = 2;

    check(
        !Protocol::validateHeader(header),
        "Invalid protocol version accepted"
    );

    header.version = Constants::PROTOCOL_VERSION;
    header.type = static_cast<MessageType>(99);

    check(
        !Protocol::validateHeader(header),
        "Invalid message type accepted"
    );

    std::cout << "[PASS] Header validation test\n";
}

// Tests serialization into exact network bytes.
static void testSerializeHeader()
{
    MessageHeader header;

    header.length = 258;
    header.type = MessageType::REQUEST;
    header.version = 1;

    uint8_t buffer[Constants::HEADER_SIZE] = {};

    const uint8_t expected[] =
    {
        0x00, 0x00, 0x01, 0x02,
        0x00, 0x03,
        0x00, 0x01
    };

    check(
        Protocol::serializeHeader(
            header, buffer, sizeof(buffer)
        ),
        "Header serialization failed"
    );

    for (uint32_t i = 0; i < Constants::HEADER_SIZE; i++)
    {
        check(
            buffer[i] == expected[i],
            "Incorrect serialized byte"
        );
    }

    std::cout << "[PASS] Header serialization test\n";
}

// Tests deserialization from known network bytes.
static void testDeserializeHeader()
{
    const uint8_t buffer[] =
    {
        0x00, 0x00, 0x01, 0x02,
        0x00, 0x04,
        0x00, 0x01
    };

    MessageHeader header;

    check(
        Protocol::deserializeHeader(
            buffer, sizeof(buffer), header
        ),
        "Header deserialization failed"
    );

    check(header.length == 258, "Invalid decoded length");

    check(
        header.type == MessageType::ASSIGN,
        "Invalid decoded message type"
    );

    check(header.version == 1, "Invalid decoded version");

    std::cout << "[PASS] Header deserialization test\n";
}

// Tests a complete serialization round trip.
static void testRoundTrip()
{
    for (uint16_t i = 1; i <= 7; i++)
    {
        MessageHeader original;

        original.length = 1000;
        original.type = static_cast<MessageType>(i);
        original.version = Constants::PROTOCOL_VERSION;

        uint8_t buffer[Constants::HEADER_SIZE] = {};

        check(
            Protocol::serializeHeader(
                original, buffer, sizeof(buffer)
            ),
            "Round trip serialization failed"
        );

        MessageHeader decoded;

        check(
            Protocol::deserializeHeader(
                buffer, sizeof(buffer), decoded
            ),
            "Round trip deserialization failed"
        );

        check(
            decoded.length == original.length,
            "Round trip length mismatch"
        );

        check(
            decoded.type == original.type,
            "Round trip message type mismatch"
        );

        check(
            decoded.version == original.version,
            "Round trip version mismatch"
        );
    }

    std::cout << "[PASS] Header round trip test\n";
}

// Tests minimum and maximum payload lengths.
static void testPayloadBoundaries()
{
    const uint32_t lengths[] =
    {
        0,
        1,
        Constants::MAX_PAYLOAD_SIZE - 1,
        Constants::MAX_PAYLOAD_SIZE
    };

    for (int i = 0; i < 4; i++)
    {
        MessageHeader original;
        original.length = lengths[i];

        uint8_t buffer[Constants::HEADER_SIZE] = {};

        check(
            Protocol::serializeHeader(
                original, buffer, sizeof(buffer)
            ),
            "Boundary serialization failed"
        );

        MessageHeader decoded;

        check(
            Protocol::deserializeHeader(
                buffer, sizeof(buffer), decoded
            ),
            "Boundary deserialization failed"
        );

        check(
            decoded.length == lengths[i],
            "Incorrect boundary length"
        );
    }

    std::cout << "[PASS] Payload boundary test\n";
}

// Tests null buffer rejection.
static void testNullBuffer()
{
    MessageHeader header;

    check(
        !Protocol::serializeHeader(
            header, nullptr, Constants::HEADER_SIZE
        ),
        "Null output buffer accepted"
    );

    check(
        !Protocol::deserializeHeader(
            nullptr, Constants::HEADER_SIZE, header
        ),
        "Null input buffer accepted"
    );

    std::cout << "[PASS] Null buffer test\n";
}

// Tests buffers smaller than the protocol header.
static void testSmallBuffer()
{
    MessageHeader header;

    uint8_t buffer[Constants::HEADER_SIZE] = {};

    for (std::size_t size = 0;
        size < Constants::HEADER_SIZE;
        size++)
    {
        check(
            !Protocol::serializeHeader(
                header, buffer, size
            ),
            "Small output buffer accepted"
        );

        check(
            !Protocol::deserializeHeader(
                buffer, size, header
            ),
            "Incomplete header accepted"
        );
    }

    std::cout << "[PASS] Small buffer test\n";
}

// Tests rejection of invalid serialized headers.
static void testInvalidSerializedHeader()
{
    // Length = 65537, Type = REQUEST, Version = 1.
    const uint8_t invalidLength[] =
    {
        0x00, 0x01, 0x00, 0x01,
        0x00, 0x03,
        0x00, 0x01
    };

    // Length = 0, Type = 99, Version = 1.
    const uint8_t invalidType[] =
    {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x63,
        0x00, 0x01
    };

    // Length = 0, Type = REQUEST, Version = 2.
    const uint8_t invalidVersion[] =
    {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x03,
        0x00, 0x02
    };

    MessageHeader header;

    check(
        !Protocol::deserializeHeader(
            invalidLength, sizeof(invalidLength), header
        ),
        "Invalid serialized length accepted"
    );

    check(
        !Protocol::deserializeHeader(
            invalidType, sizeof(invalidType), header
        ),
        "Invalid serialized type accepted"
    );

    check(
        !Protocol::deserializeHeader(
            invalidVersion, sizeof(invalidVersion), header
        ),
        "Invalid serialized version accepted"
    );

    std::cout << "[PASS] Invalid serialized header test\n";
}

// Tests preservation of the output header on failure.
static void testUnchangedHeader()
{
    MessageHeader header;

    header.length = 500;
    header.type = MessageType::RESULT;
    header.version = 1;

    const uint8_t invalidBuffer[] =
    {
        0x00, 0x00, 0x00, 0x10,
        0x00, 0x03,
        0x00, 0x02
    };

    check(
        !Protocol::deserializeHeader(
            invalidBuffer,
            sizeof(invalidBuffer),
            header
        ),
        "Invalid header accepted"
    );

    check(header.length == 500, "Length was modified");

    check(
        header.type == MessageType::RESULT,
        "Message type was modified"
    );

    check(header.version == 1, "Version was modified");

    std::cout << "[PASS] Unchanged header test\n";
}

// Tests preservation of the output buffer on failure.
static void testUnchangedBuffer()
{
    uint8_t buffer[Constants::HEADER_SIZE];

    for (uint32_t i = 0; i < Constants::HEADER_SIZE; i++)
    {
        buffer[i] = 0xAA;
    }

    MessageHeader header;

    header.length = Constants::MAX_PAYLOAD_SIZE + 1;

    check(
        !Protocol::serializeHeader(
            header, buffer, sizeof(buffer)
        ),
        "Invalid header serialized"
    );

    for (uint32_t i = 0; i < Constants::HEADER_SIZE; i++)
    {
        check(buffer[i] == 0xAA, "Output buffer modified");
    }

    std::cout << "[PASS] Unchanged buffer test\n";
}

// Runs all protocol header tests.
void runProtocolTests()
{
    testValidMessageTypes();
    testInvalidMessageTypes();
    testHeaderValidation();
    testSerializeHeader();
    testDeserializeHeader();
    testRoundTrip();
    testPayloadBoundaries();
    testNullBuffer();
    testSmallBuffer();
    testInvalidSerializedHeader();
    testUnchangedHeader();
    testUnchangedBuffer();

    std::cout << "All protocol header tests passed.\n";
}