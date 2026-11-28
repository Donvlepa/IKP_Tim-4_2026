#include <iostream>
#include <cstdlib>
#include <cstring>

#include "MessageBuffer.h"
#include "Protocol.h"
#include "PayloadCodec.h"
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

// Builds a complete message for testing.
static std::size_t buildMessage(
    MessageType type,
    const uint8_t* payload,
    uint32_t payloadSize,
    uint8_t* buffer,
    std::size_t bufferSize
)
{
    const std::size_t messageSize =
        Constants::HEADER_SIZE + payloadSize;

    check(bufferSize >= messageSize,
        "Test message buffer is too small");

    MessageHeader header;

    header.length = payloadSize;
    header.type = type;
    header.version = Constants::PROTOCOL_VERSION;

    check(Protocol::serializeHeader(
        header, buffer, bufferSize),
        "Test header serialization failed");

    if (payloadSize > 0)
    {
        std::memcpy(
            buffer + Constants::HEADER_SIZE,
            payload,
            payloadSize
        );
    }

    return messageSize;
}

// Builds a valid request message.
static std::size_t buildRequestMessage(
    uint8_t* buffer,
    std::size_t bufferSize
)
{
    const uint8_t data[] = { 10, 20, 30 };

    RequestPayload request;

    request.clientRequestId = 123;
    request.priority = Priority::HIGH;
    request.dataSize = sizeof(data);
    request.data = data;

    uint8_t payload[32] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeRequest(
        request, payload, sizeof(payload), written),
        "Test request serialization failed");

    return buildMessage(
        MessageType::REQUEST,
        payload,
        static_cast<uint32_t>(written),
        buffer,
        bufferSize
    );
}

// Writes a raw header for malformed message tests.
static void writeRawHeader(
    uint8_t* buffer,
    uint32_t length,
    uint16_t type,
    uint16_t version
)
{
    buffer[0] = static_cast<uint8_t>(length >> 24);
    buffer[1] = static_cast<uint8_t>(length >> 16);
    buffer[2] = static_cast<uint8_t>(length >> 8);
    buffer[3] = static_cast<uint8_t>(length);

    buffer[4] = static_cast<uint8_t>(type >> 8);
    buffer[5] = static_cast<uint8_t>(type);

    buffer[6] = static_cast<uint8_t>(version >> 8);
    buffer[7] = static_cast<uint8_t>(version);
}

// Tests the initial buffer state.
static void testInitialState()
{
    MessageBuffer buffer;

    MessageHeader header;
    const uint8_t* payload = nullptr;

    check(!buffer.hasMessage(),
        "New buffer contains a message");

    check(!buffer.hasError(),
        "New buffer contains an error");

    check(buffer.getBufferedSize() == 0,
        "New buffer is not empty");

    check(!buffer.getMessage(header, payload),
        "Empty buffer returned a message");

    check(!buffer.consumeMessage(),
        "Empty buffer consumed a message");

    std::cout << "[PASS] Initial buffer state test\n";
}

// Tests complete message reception.
static void testCompleteMessage()
{
    MessageBuffer buffer;

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildRequestMessage(message, sizeof(message));

    std::size_t consumed = 0;

    check(buffer.append(
        message, messageSize, consumed)
        == MessageBufferStatus::MessageReady,
        "Complete message was not detected");

    check(consumed == messageSize,
        "Incorrect consumed byte count");

    check(buffer.hasMessage(),
        "Complete message is not available");

    check(buffer.getBufferedSize() == messageSize,
        "Incorrect buffered size");

    MessageHeader header;
    const uint8_t* payload = nullptr;

    check(buffer.getMessage(header, payload),
        "Could not retrieve complete message");

    check(header.type == MessageType::REQUEST,
        "Incorrect received message type");

    check(header.length == 12,
        "Incorrect received payload length");

    check(header.version == Constants::PROTOCOL_VERSION,
        "Incorrect received protocol version");

    RequestPayload decoded;

    check(PayloadCodec::deserializeRequest(
        payload, header.length, decoded),
        "Received request decoding failed");

    check(decoded.clientRequestId == 123,
        "Incorrect received request ID");

    check(decoded.priority == Priority::HIGH,
        "Incorrect received priority");

    check(decoded.dataSize == 3,
        "Incorrect received data size");

    check(decoded.data[0] == 10 &&
        decoded.data[1] == 20 &&
        decoded.data[2] == 30,
        "Incorrect received data");

    check(buffer.consumeMessage(),
        "Message consumption failed");

    check(!buffer.hasMessage(),
        "Consumed message is still available");

    check(buffer.getBufferedSize() == 0,
        "Buffer was not cleared");

    std::cout << "[PASS] Complete message test\n";
}

// Tests a header received in multiple parts.
static void testFragmentedHeader()
{
    MessageBuffer buffer;

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildRequestMessage(message, sizeof(message));

    std::size_t consumed = 0;

    check(buffer.append(message, 3, consumed)
        == MessageBufferStatus::NeedMoreData,
        "Partial header was accepted as complete");

    check(consumed == 3,
        "Incorrect first fragment size");

    check(buffer.getBufferedSize() == 3,
        "Incorrect buffered header size");

    check(buffer.append(message + 3, 5, consumed)
        == MessageBufferStatus::NeedMoreData,
        "Header without payload was accepted");

    check(consumed == 5,
        "Incorrect second fragment size");

    check(buffer.getBufferedSize() ==
        Constants::HEADER_SIZE,
        "Complete header was not stored");

    check(buffer.append(
        message + Constants::HEADER_SIZE,
        messageSize - Constants::HEADER_SIZE,
        consumed)
        == MessageBufferStatus::MessageReady,
        "Fragmented message was not completed");

    check(consumed ==
        messageSize - Constants::HEADER_SIZE,
        "Incorrect payload fragment size");

    std::cout << "[PASS] Fragmented header test\n";
}

// Tests a payload received in multiple parts.
static void testFragmentedPayload()
{
    MessageBuffer buffer;

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildRequestMessage(message, sizeof(message));

    std::size_t consumed = 0;

    const std::size_t firstPart =
        Constants::HEADER_SIZE + 4;

    check(buffer.append(
        message, firstPart, consumed)
        == MessageBufferStatus::NeedMoreData,
        "Incomplete payload was accepted");

    check(consumed == firstPart,
        "Incorrect first payload fragment size");

    check(buffer.append(
        message + firstPart,
        messageSize - firstPart,
        consumed)
        == MessageBufferStatus::MessageReady,
        "Fragmented payload was not completed");

    check(buffer.hasMessage(),
        "Completed message is unavailable");

    std::cout << "[PASS] Fragmented payload test\n";
}

// Tests every possible two-part message split.
static void testAllSplitPositions()
{
    MessageBuffer buffer;

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildRequestMessage(message, sizeof(message));

    for (std::size_t split = 1;
        split < messageSize;
        ++split)
    {
        buffer.reset();

        std::size_t consumed = 0;

        check(buffer.append(
            message, split, consumed)
            == MessageBufferStatus::NeedMoreData,
            "Incomplete split was accepted");

        check(consumed == split,
            "Incorrect first split byte count");

        check(buffer.append(
            message + split,
            messageSize - split,
            consumed)
            == MessageBufferStatus::MessageReady,
            "Split message was not completed");

        check(consumed == messageSize - split,
            "Incorrect second split byte count");

        check(buffer.consumeMessage(),
            "Split message consumption failed");
    }

    std::cout << "[PASS] All split positions test\n";
}

// Tests reception of one byte at a time.
static void testByteByByte()
{
    MessageBuffer buffer;

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildRequestMessage(message, sizeof(message));

    for (std::size_t i = 0; i < messageSize; ++i)
    {
        std::size_t consumed = 0;

        const MessageBufferStatus status =
            buffer.append(message + i, 1, consumed);

        check(consumed == 1,
            "Single byte was not consumed");

        if (i + 1 < messageSize)
        {
            check(status ==
                MessageBufferStatus::NeedMoreData,
                "Message completed too early");
        }
        else
        {
            check(status ==
                MessageBufferStatus::MessageReady,
                "Message did not complete");
        }
    }

    check(buffer.hasMessage(),
        "Byte-by-byte message is unavailable");

    std::cout << "[PASS] Byte-by-byte reception test\n";
}

// Tests messages without payload data.
static void testEmptyPayloadMessages()
{
    MessageBuffer buffer;

    const MessageType types[] =
    {
        MessageType::REGISTER_UR,
        MessageType::HEARTBEAT
    };

    for (int i = 0; i < 2; ++i)
    {
        uint8_t message[Constants::HEADER_SIZE] = {};

        const std::size_t messageSize =
            buildMessage(
                types[i],
                nullptr,
                0,
                message,
                sizeof(message)
            );

        std::size_t consumed = 0;

        check(buffer.append(
            message, messageSize, consumed)
            == MessageBufferStatus::MessageReady,
            "Empty payload message was not accepted");

        check(consumed == Constants::HEADER_SIZE,
            "Incorrect empty message size");

        MessageHeader header;
        const uint8_t* payload = message;

        check(buffer.getMessage(header, payload),
            "Empty payload message retrieval failed");

        check(header.type == types[i],
            "Incorrect empty message type");

        check(header.length == 0,
            "Empty message contains payload");

        check(payload == nullptr,
            "Empty payload pointer is not null");

        check(buffer.consumeMessage(),
            "Empty message consumption failed");
    }

    std::cout << "[PASS] Empty payload messages test\n";
}

// Tests all supported protocol message types.
static void testAllMessageTypes()
{
    MessageBuffer buffer;

    const MessageType types[] =
    {
        MessageType::REGISTER_UR,
        MessageType::REGISTER_WR,
        MessageType::REQUEST,
        MessageType::ASSIGN,
        MessageType::HEARTBEAT,
        MessageType::RESULT,
        MessageType::RESPONSE
    };

    const uint32_t sizes[] =
    {
        Constants::REGISTER_UR_PAYLOAD_SIZE,
        Constants::REGISTER_WR_PAYLOAD_SIZE,
        Constants::REQUEST_FIXED_SIZE,
        Constants::ASSIGN_FIXED_SIZE,
        Constants::HEARTBEAT_PAYLOAD_SIZE,
        Constants::RESULT_FIXED_SIZE,
        Constants::RESPONSE_FIXED_SIZE
    };

    for (int i = 0; i < 7; ++i)
    {
        uint8_t payload[32] = {};

        switch (types[i])
        {
        case MessageType::REGISTER_WR:
            payload[7] = 1;
            break;

        case MessageType::REQUEST:
            payload[7] = 1;
            payload[8] =
                static_cast<uint8_t>(Priority::HIGH);
            break;

        case MessageType::ASSIGN:
            payload[7] = 1;
            payload[15] = 1;
            payload[16] =
                static_cast<uint8_t>(Priority::MEDIUM);
            break;

        case MessageType::RESULT:
            payload[7] = 1;
            payload[15] = 1;
            break;

        case MessageType::RESPONSE:
            payload[7] = 1;
            payload[8] = static_cast<uint8_t>(
                ResponseStatus::SUCCESS
                );
            break;

        default:
            break;
        }

        uint8_t message[64] = {};

        const std::size_t messageSize =
            buildMessage(
                types[i],
                payload,
                sizes[i],
                message,
                sizeof(message)
            );

        std::size_t consumed = 0;

        check(buffer.append(
            message, messageSize, consumed)
            == MessageBufferStatus::MessageReady,
            "Valid message type was rejected");

        MessageHeader header;
        const uint8_t* receivedPayload = nullptr;

        check(buffer.getMessage(
            header, receivedPayload),
            "Message retrieval failed");

        check(header.type == types[i],
            "Incorrect message type");

        check(header.length == sizes[i],
            "Incorrect message payload size");

        check(consumed == messageSize,
            "Incorrect consumed message size");

        if (sizes[i] > 0)
        {
            check(receivedPayload != nullptr,
                "Non-empty payload pointer is null");

            check(std::memcmp(
                payload, receivedPayload, sizes[i]) == 0,
                "Received payload mismatch");
        }
        else
        {
            check(receivedPayload == nullptr,
                "Empty payload pointer is not null");
        }

        check(buffer.consumeMessage(),
            "Message consumption failed");
    }

    std::cout << "[PASS] All seven message types test\n";
}

// Tests multiple messages received together.
static void testMultipleMessages()
{
    MessageBuffer buffer;

    uint8_t data[128] = {};

    const std::size_t firstSize =
        buildMessage(
            MessageType::HEARTBEAT,
            nullptr,
            0,
            data,
            sizeof(data)
        );

    const std::size_t secondSize =
        buildRequestMessage(
            data + firstSize,
            sizeof(data) - firstSize
        );

    const std::size_t thirdSize =
        buildMessage(
            MessageType::REGISTER_UR,
            nullptr,
            0,
            data + firstSize + secondSize,
            sizeof(data) - firstSize - secondSize
        );

    const std::size_t totalSize =
        firstSize + secondSize + thirdSize;

    const MessageType expectedTypes[] =
    {
        MessageType::HEARTBEAT,
        MessageType::REQUEST,
        MessageType::REGISTER_UR
    };

    std::size_t offset = 0;
    int receivedCount = 0;

    while (offset < totalSize)
    {
        std::size_t consumed = 0;

        check(buffer.append(
            data + offset,
            totalSize - offset,
            consumed)
            == MessageBufferStatus::MessageReady,
            "Combined message reception failed");

        check(consumed > 0,
            "No bytes were consumed");

        check(receivedCount < 3,
            "Too many messages were extracted");

        MessageHeader header;
        const uint8_t* payload = nullptr;

        check(buffer.getMessage(header, payload),
            "Combined message retrieval failed");

        check(header.type == expectedTypes[receivedCount],
            "Incorrect combined message order");

        offset += consumed;
        ++receivedCount;

        check(buffer.consumeMessage(),
            "Combined message consumption failed");
    }

    check(receivedCount == 3,
        "Incorrect number of received messages");

    check(offset == totalSize,
        "Not all input bytes were processed");

    std::cout << "[PASS] Multiple messages test\n";
}

// Tests a complete message followed by a partial message.
static void testPartialNextMessage()
{
    MessageBuffer buffer;

    uint8_t data[128] = {};

    const std::size_t firstSize =
        buildMessage(
            MessageType::HEARTBEAT,
            nullptr,
            0,
            data,
            sizeof(data)
        );

    const std::size_t secondSize =
        buildRequestMessage(
            data + firstSize,
            sizeof(data) - firstSize
        );

    const std::size_t partialSize = 5;

    std::size_t consumed = 0;

    check(buffer.append(
        data, firstSize + partialSize, consumed)
        == MessageBufferStatus::MessageReady,
        "First message was not completed");

    check(consumed == firstSize,
        "Next message bytes were consumed too early");

    check(buffer.consumeMessage(),
        "First message consumption failed");

    check(buffer.append(
        data + firstSize, partialSize, consumed)
        == MessageBufferStatus::NeedMoreData,
        "Partial next message was accepted");

    check(consumed == partialSize,
        "Incorrect partial message byte count");

    check(buffer.append(
        data + firstSize + partialSize,
        secondSize - partialSize,
        consumed)
        == MessageBufferStatus::MessageReady,
        "Second message was not completed");

    MessageHeader header;
    const uint8_t* payload = nullptr;

    check(buffer.getMessage(header, payload),
        "Second message retrieval failed");

    check(header.type == MessageType::REQUEST,
        "Incorrect second message type");

    std::cout << "[PASS] Partial next message test\n";
}

// Tests that an unread message is not overwritten.
static void testUnreadMessageProtection()
{
    MessageBuffer buffer;

    uint8_t first[64] = {};
    uint8_t second[Constants::HEADER_SIZE] = {};

    const std::size_t firstSize =
        buildRequestMessage(first, sizeof(first));

    const std::size_t secondSize =
        buildMessage(
            MessageType::HEARTBEAT,
            nullptr,
            0,
            second,
            sizeof(second)
        );

    std::size_t consumed = 0;

    check(buffer.append(first, firstSize, consumed)
        == MessageBufferStatus::MessageReady,
        "First message reception failed");

    check(buffer.append(second, secondSize, consumed)
        == MessageBufferStatus::MessageReady,
        "Unread message state changed");

    check(consumed == 0,
        "New bytes overwrote unread message");

    MessageHeader header;
    const uint8_t* payload = nullptr;

    check(buffer.getMessage(header, payload),
        "Unread message was lost");

    check(header.type == MessageType::REQUEST,
        "Unread message was replaced");

    check(buffer.consumeMessage(),
        "Unread message consumption failed");

    check(buffer.append(second, secondSize, consumed)
        == MessageBufferStatus::MessageReady,
        "Next message reception failed");

    std::cout << "[PASS] Unread message protection test\n";
}

// Tests malformed protocol headers.
static void testInvalidHeaders()
{
    MessageBuffer buffer;

    uint8_t message[Constants::HEADER_SIZE] = {};
    std::size_t consumed = 0;

    const uint16_t heartbeatType =
        static_cast<uint16_t>(MessageType::HEARTBEAT);

    // Invalid protocol version.
    writeRawHeader(
        message,
        0,
        heartbeatType,
        Constants::PROTOCOL_VERSION + 1
    );

    check(buffer.append(
        message, sizeof(message), consumed)
        == MessageBufferStatus::InvalidMessage,
        "Invalid protocol version accepted");

    check(buffer.hasError(),
        "Invalid version did not set error state");

    buffer.reset();

    // Unsupported message type.
    writeRawHeader(
        message,
        0,
        65535,
        Constants::PROTOCOL_VERSION
    );

    check(buffer.append(
        message, sizeof(message), consumed)
        == MessageBufferStatus::InvalidMessage,
        "Unsupported message type accepted");

    buffer.reset();

    // Payload length exceeds protocol limit.
    writeRawHeader(
        message,
        Constants::MAX_PAYLOAD_SIZE + 1,
        static_cast<uint16_t>(MessageType::REQUEST),
        Constants::PROTOCOL_VERSION
    );

    check(buffer.append(
        message, sizeof(message), consumed)
        == MessageBufferStatus::InvalidMessage,
        "Oversized protocol header accepted");

    check(!buffer.hasMessage(),
        "Invalid header produced a message");

    std::cout << "[PASS] Invalid headers test\n";
}

// Tests invalid message-specific payload lengths.
static void testInvalidPayloadLengths()
{
    MessageBuffer buffer;

    const MessageType types[] =
    {
        MessageType::REGISTER_UR,
        MessageType::REGISTER_WR,
        MessageType::REQUEST,
        MessageType::ASSIGN,
        MessageType::HEARTBEAT,
        MessageType::RESULT,
        MessageType::RESPONSE,
        MessageType::REQUEST,
        MessageType::RESPONSE
    };

    const uint32_t invalidLengths[] =
    {
        1,
        7,
        Constants::REQUEST_FIXED_SIZE - 1,
        Constants::ASSIGN_FIXED_SIZE - 1,
        1,
        Constants::RESULT_FIXED_SIZE - 1,
        Constants::RESPONSE_FIXED_SIZE - 1,

        Constants::REQUEST_FIXED_SIZE +
        Constants::MAX_REQUEST_DATA_SIZE + 1,

        Constants::RESPONSE_FIXED_SIZE +
        Constants::MAX_RESULT_DATA_SIZE + 1
    };

    for (int i = 0; i < 9; ++i)
    {
        buffer.reset();

        uint8_t header[Constants::HEADER_SIZE] = {};

        writeRawHeader(
            header,
            invalidLengths[i],
            static_cast<uint16_t>(types[i]),
            Constants::PROTOCOL_VERSION
        );

        std::size_t consumed = 0;

        check(buffer.append(
            header, sizeof(header), consumed)
            == MessageBufferStatus::InvalidMessage,
            "Invalid payload length accepted");

        check(buffer.hasError(),
            "Invalid payload length did not set error");

        check(!buffer.hasMessage(),
            "Invalid length produced a message");
    }

    std::cout << "[PASS] Invalid payload lengths test\n";
}

// Tests payload content validation.
static void testInvalidPayloadContent()
{
    MessageBuffer buffer;

    uint8_t payload[32] = {};
    uint8_t message[64] = {};

    std::size_t consumed = 0;

    // Invalid REQUEST priority.
    payload[8] = 255;

    std::size_t messageSize =
        buildMessage(
            MessageType::REQUEST,
            payload,
            Constants::REQUEST_FIXED_SIZE,
            message,
            sizeof(message)
        );

    check(buffer.append(
        message, messageSize, consumed)
        == MessageBufferStatus::InvalidMessage,
        "Invalid request priority accepted");

    buffer.reset();

    // Invalid ASSIGN priority.
    payload[16] = 255;

    messageSize = buildMessage(
        MessageType::ASSIGN,
        payload,
        Constants::ASSIGN_FIXED_SIZE,
        message,
        sizeof(message)
    );

    check(buffer.append(
        message, messageSize, consumed)
        == MessageBufferStatus::InvalidMessage,
        "Invalid assignment priority accepted");

    buffer.reset();

    // Invalid RESPONSE status.
    payload[8] = 255;

    messageSize = buildMessage(
        MessageType::RESPONSE,
        payload,
        Constants::RESPONSE_FIXED_SIZE,
        message,
        sizeof(message)
    );

    check(buffer.append(
        message, messageSize, consumed)
        == MessageBufferStatus::InvalidMessage,
        "Invalid response status accepted");

    buffer.reset();

    // Cancelled response must not contain result data.
    payload[8] =
        static_cast<uint8_t>(ResponseStatus::CANCELLED);

    payload[9] = 10;

    messageSize = buildMessage(
        MessageType::RESPONSE,
        payload,
        Constants::RESPONSE_FIXED_SIZE + 1,
        message,
        sizeof(message)
    );

    check(buffer.append(
        message, messageSize, consumed)
        == MessageBufferStatus::InvalidMessage,
        "Cancelled response with data accepted");

    std::cout << "[PASS] Invalid payload content test\n";
}

// Tests maximum-sized messages and combined reception.
static void testMaximumMessages()
{
    MessageBuffer buffer;

    static uint8_t payload[
        Constants::MAX_PAYLOAD_SIZE
    ];

    static uint8_t messages[
        Constants::MAX_MESSAGE_SIZE * 2
    ];

    std::memset(payload, 0, sizeof(payload));

    payload[7] = 1;
    payload[15] = 1;

    payload[16] =
        static_cast<uint8_t>(Priority::HIGH);

    payload[Constants::MAX_PAYLOAD_SIZE - 1] = 123;

    const std::size_t firstSize =
        buildMessage(
            MessageType::ASSIGN,
            payload,
            Constants::MAX_PAYLOAD_SIZE,
            messages,
            sizeof(messages)
        );

    const std::size_t secondSize =
        buildMessage(
            MessageType::ASSIGN,
            payload,
            Constants::MAX_PAYLOAD_SIZE,
            messages + firstSize,
            sizeof(messages) - firstSize
        );

    std::size_t consumed = 0;

    check(buffer.append(
        messages,
        firstSize + secondSize,
        consumed)
        == MessageBufferStatus::MessageReady,
        "Maximum message reception failed");

    check(consumed == Constants::MAX_MESSAGE_SIZE,
        "Incorrect maximum message byte count");

    check(buffer.getBufferedSize() ==
        Constants::MAX_MESSAGE_SIZE,
        "Incorrect maximum buffered size");

    MessageHeader header;
    const uint8_t* receivedPayload = nullptr;

    check(buffer.getMessage(header, receivedPayload),
        "Maximum message retrieval failed");

    check(header.length == Constants::MAX_PAYLOAD_SIZE,
        "Incorrect maximum payload length");

    AssignPayload decoded;

    check(PayloadCodec::deserializeAssign(
        receivedPayload, header.length, decoded),
        "Maximum assignment decoding failed");

    check(decoded.dataSize ==
        Constants::MAX_REQUEST_DATA_SIZE,
        "Incorrect maximum assignment data size");

    check(decoded.data[decoded.dataSize - 1] == 123,
        "Last byte of maximum payload was lost");

    check(buffer.consumeMessage(),
        "Maximum message consumption failed");

    check(buffer.append(
        messages + firstSize,
        secondSize,
        consumed)
        == MessageBufferStatus::MessageReady,
        "Second maximum message reception failed");

    check(consumed == secondSize,
        "Incorrect second maximum message size");

    check(buffer.consumeMessage(),
        "Second maximum message consumption failed");

    std::cout << "[PASS] Maximum messages test\n";
}

// Tests invalid input and empty append operations.
static void testInvalidInput()
{
    MessageBuffer buffer;

    std::size_t consumed = 99;

    check(buffer.append(nullptr, 1, consumed)
        == MessageBufferStatus::InvalidInput,
        "Null input with non-zero size accepted");

    check(consumed == 0,
        "Invalid input consumed bytes");

    check(!buffer.hasError(),
        "Invalid input caused a protocol error");

    check(buffer.getBufferedSize() == 0,
        "Invalid input modified the buffer");

    check(buffer.append(nullptr, 0, consumed)
        == MessageBufferStatus::NeedMoreData,
        "Empty append returned incorrect status");

    check(consumed == 0,
        "Empty append consumed bytes");

    std::cout << "[PASS] Invalid input test\n";
}

// Tests persistent errors and explicit reset.
static void testErrorAndReset()
{
    MessageBuffer buffer;

    uint8_t invalidHeader[
        Constants::HEADER_SIZE
    ] = {};

        writeRawHeader(
            invalidHeader,
            0,
            65535,
            Constants::PROTOCOL_VERSION
        );

        std::size_t consumed = 0;

        check(buffer.append(
            invalidHeader,
            sizeof(invalidHeader),
            consumed)
            == MessageBufferStatus::InvalidMessage,
            "Malformed header accepted");

        uint8_t message[64] = {};

        const std::size_t messageSize =
            buildRequestMessage(message, sizeof(message));

        check(buffer.append(
            message, messageSize, consumed)
            == MessageBufferStatus::InvalidMessage,
            "Buffer accepted data after protocol error");

        check(consumed == 0,
            "Error state consumed new data");

        check(!buffer.consumeMessage(),
            "Invalid message was consumed");

        buffer.reset();

        check(!buffer.hasError(),
            "Reset did not clear error state");

        check(!buffer.hasMessage(),
            "Reset did not clear message state");

        check(buffer.getBufferedSize() == 0,
            "Reset did not clear buffered size");

        check(buffer.append(
            message, messageSize, consumed)
            == MessageBufferStatus::MessageReady,
            "Buffer could not be reused after reset");

        check(buffer.consumeMessage(),
            "Message consumption after reset failed");

        // Reset an incomplete header.
        check(buffer.append(message, 3, consumed)
            == MessageBufferStatus::NeedMoreData,
            "Partial message reception failed");

        buffer.reset();

        check(buffer.getBufferedSize() == 0,
            "Partial message was not cleared");

        check(buffer.append(
            message, messageSize, consumed)
            == MessageBufferStatus::MessageReady,
            "Reception failed after partial reset");

        std::cout << "[PASS] Error state and reset test\n";
}

// Runs all message buffer tests.
void runMessageBufferTests()
{
    testInitialState();
    testCompleteMessage();
    testFragmentedHeader();
    testFragmentedPayload();
    testAllSplitPositions();
    testByteByByte();
    testEmptyPayloadMessages();
    testAllMessageTypes();
    testMultipleMessages();
    testPartialNextMessage();
    testUnreadMessageProtection();
    testInvalidHeaders();
    testInvalidPayloadLengths();
    testInvalidPayloadContent();
    testMaximumMessages();
    testInvalidInput();
    testErrorAndReset();

    std::cout << "All message buffer tests passed.\n";
}