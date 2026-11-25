#include <iostream>
#include <cassert>
#include "MessageHeader.h"
#include "TestSuites.h"

// Checks the size of individual header fields.
static_assert(sizeof(uint32_t) == 4,
    "Invalid length field size");

static_assert(sizeof(MessageType) == 2,
    "Invalid message type field size");

static_assert(sizeof(uint16_t) == 2,
    "Invalid version field size");

static_assert(
    sizeof(uint32_t) +
    sizeof(MessageType) +
    sizeof(uint16_t) == Constants::HEADER_SIZE,
    "Invalid protocol header size"
    );

// Tests default header initialization.
static void testDefaultMessageHeader()
{
    MessageHeader header;

    assert(header.length == 0);
    assert(header.type == MessageType::REQUEST);
    assert(header.version == Constants::PROTOCOL_VERSION);

    std::cout << "Default message header test passed.\n";
}

// Tests header initialization with custom values.
static void testMessageHeaderInitialization()
{
    MessageHeader header;

    header.length = 100;
    header.type = MessageType::ASSIGN;
    header.version = Constants::PROTOCOL_VERSION;

    assert(header.length == 100);
    assert(header.type == MessageType::ASSIGN);
    assert(header.version == 1);

    std::cout << "Message header initialization test passed.\n";
}

// Tests storage of the maximum payload length.
static void testMaximumPayloadLength()
{
    MessageHeader header;

    header.length = Constants::MAX_PAYLOAD_SIZE;

    assert(header.length == 65536);

    std::cout << "Maximum payload length test passed.\n";
}

// Tests calculation of the total message size.
static void testTotalMessageSize()
{
    MessageHeader header;

    header.length = 20;

    uint32_t totalSize =
        Constants::HEADER_SIZE + header.length;

    assert(totalSize == 28);

    std::cout << "Total message size test passed.\n";
}

// Tests storage of all supported message types.
static void testMessageTypes()
{
    MessageHeader header;

    MessageType types[] =
    {
        MessageType::REGISTER_UR,
        MessageType::REGISTER_WR,
        MessageType::REQUEST,
        MessageType::ASSIGN,
        MessageType::HEARTBEAT,
        MessageType::RESULT,
        MessageType::RESPONSE
    };

    for (int i = 0; i < 7; i++)
    {
        header.type = types[i];

        assert(header.type == types[i]);
        assert(static_cast<uint16_t>(header.type) == i + 1);
    }

    std::cout << "Message types test passed.\n";
}

// Runs all message header tests.
void runMessageHeaderTests()
{
    testDefaultMessageHeader();
    testMessageHeaderInitialization();
    testMaximumPayloadLength();
    testTotalMessageSize();
    testMessageTypes();
}