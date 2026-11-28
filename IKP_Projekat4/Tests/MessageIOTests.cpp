#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <cstdlib>
#include <cstring>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "SendUtils.h"
#include "MessageIO.h"
#include "Protocol.h"
#include "PayloadCodec.h"
#include "Constants.h"
#include "TestSuites.h"

// Stores a pair of connected TCP sockets.
struct MessageIOTestConnection
{
    SOCKET sender = INVALID_SOCKET;
    SOCKET receiver = INVALID_SOCKET;
};

// Checks a test condition.
static void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";
        std::abort();
    }
}

// Checks a Winsock operation result.
static void checkResult(int result, const char* message)
{
    if (result != 0)
    {
        std::cerr << "[FAIL] " << message
            << " | Winsock error: "
            << result << "\n";

        std::abort();
    }
}

// Waits until a socket becomes readable.
static void waitReadable(
    SOCKET socketHandle,
    uint32_t timeoutMs = 3000
)
{
    fd_set readSet;

    FD_ZERO(&readSet);
    FD_SET(socketHandle, &readSet);

    timeval timeout = {};

    timeout.tv_sec =
        static_cast<long>(timeoutMs / 1000);

    timeout.tv_usec =
        static_cast<long>((timeoutMs % 1000) * 1000);

    const int result = select(
        0,
        &readSet,
        nullptr,
        nullptr,
        &timeout
    );

    check(
        result == 1 &&
        FD_ISSET(socketHandle, &readSet),
        "Socket did not become readable"
    );
}

// Creates a local TCP connection.
static void createConnection(MessageIOTestConnection& connection)
{
    SOCKET listener = INVALID_SOCKET;

    checkResult(
        SocketUtils::createTcpSocket(listener),
        "Listener creation failed"
    );

    checkResult(
        SocketUtils::bindSocket(
            listener, "127.0.0.1", 0),
        "Listener binding failed"
    );

    checkResult(
        SocketUtils::listenSocket(listener),
        "Listen operation failed"
    );

    uint16_t port = 0;

    checkResult(
        SocketUtils::getLocalPort(listener, port),
        "Local port retrieval failed"
    );

    checkResult(
        SocketUtils::createTcpSocket(connection.sender),
        "Sender creation failed"
    );

    checkResult(
        SocketUtils::connectSocket(
            connection.sender, "127.0.0.1", port),
        "Sender connection failed"
    );

    waitReadable(listener);

    checkResult(
        SocketUtils::acceptSocket(
            listener, connection.receiver),
        "Receiver acceptance failed"
    );

    checkResult(
        SocketUtils::setNonBlocking(
            connection.sender, true),
        "Sender non-blocking configuration failed"
    );

    checkResult(
        SocketUtils::setNonBlocking(
            connection.receiver, true),
        "Receiver non-blocking configuration failed"
    );

    checkResult(
        SocketUtils::closeSocket(listener),
        "Listener cleanup failed"
    );
}

// Closes both connected sockets.
static void closeConnection(MessageIOTestConnection& connection)
{
    checkResult(
        SocketUtils::closeSocket(connection.sender),
        "Sender cleanup failed"
    );

    checkResult(
        SocketUtils::closeSocket(connection.receiver),
        "Receiver cleanup failed"
    );
}

// Sends raw TCP data for reception tests.
static void sendRaw(
    SOCKET socketHandle,
    const uint8_t* data,
    std::size_t size
)
{
    std::size_t bytesSent = 0;

    checkResult(
        SendUtils::sendAll(
            socketHandle,
            data,
            size,
            bytesSent,
            3000
        ),
        "Raw data sending failed"
    );

    check(
        bytesSent == size,
        "Raw data was not fully sent"
    );
}

// Builds a complete protocol message for testing.
static std::size_t buildMessage(
    MessageType type,
    const uint8_t* payload,
    uint32_t payloadSize,
    uint8_t* buffer,
    std::size_t bufferSize
)
{
    check(
        payloadSize <= Constants::MAX_PAYLOAD_SIZE,
        "Test payload is too large"
    );

    const std::size_t messageSize =
        Constants::HEADER_SIZE + payloadSize;

    check(
        bufferSize >= messageSize,
        "Test message buffer is too small"
    );

    MessageHeader header = {};

    header.length = payloadSize;
    header.type = type;
    header.version = Constants::PROTOCOL_VERSION;

    check(
        Protocol::serializeHeader(
            header,
            buffer,
            Constants::HEADER_SIZE
        ),
        "Test header serialization failed"
    );

    if (payloadSize > 0)
    {
        check(
            payload != nullptr,
            "Test payload pointer is null"
        );

        std::memcpy(
            buffer + Constants::HEADER_SIZE,
            payload,
            payloadSize
        );
    }

    return messageSize;
}

// Waits for a message or a terminal receive status.
static ReceiveStatus waitForReceive(
    MessageReceiver& receiver,
    SOCKET socketHandle,
    int& socketError
)
{
    constexpr uint32_t TIMEOUT_MS = 3000;

    const ULONGLONG startTime = GetTickCount64();

    while (true)
    {
        const ReceiveStatus status =
            receiver.receive(socketHandle, socketError);

        if (status != ReceiveStatus::WouldBlock)
        {
            return status;
        }

        const ULONGLONG elapsedTime =
            GetTickCount64() - startTime;

        check(
            elapsedTime < TIMEOUT_MS,
            "Message reception timed out"
        );

        const uint32_t remainingTimeout =
            TIMEOUT_MS -
            static_cast<uint32_t>(elapsedTime);

        waitReadable(
            socketHandle,
            remainingTimeout
        );
    }
}

// Verifies and consumes one received message.
static void expectMessage(
    MessageReceiver& receiver,
    SOCKET socketHandle,
    MessageType expectedType,
    const uint8_t* expectedPayload,
    std::size_t expectedSize
)
{
    int socketError = 0;

    check(
        waitForReceive(
            receiver,
            socketHandle,
            socketError
        ) == ReceiveStatus::MessageReady,
        "Expected message was not received"
    );

    check(
        socketError == 0,
        "Unexpected socket error"
    );

    check(
        receiver.hasMessage(),
        "Complete message is unavailable"
    );

    MessageHeader header = {};
    const uint8_t* payload = nullptr;

    check(
        receiver.getMessage(header, payload),
        "Message retrieval failed"
    );

    check(
        header.type == expectedType,
        "Incorrect received message type"
    );

    check(
        header.version == Constants::PROTOCOL_VERSION,
        "Incorrect received protocol version"
    );

    check(
        header.length == expectedSize,
        "Incorrect received payload size"
    );

    if (expectedSize > 0)
    {
        check(
            payload != nullptr,
            "Received payload pointer is null"
        );

        check(
            std::memcmp(
                payload,
                expectedPayload,
                expectedSize
            ) == 0,
            "Received payload data mismatch"
        );
    }
    else
    {
        check(
            payload == nullptr,
            "Empty payload pointer is not null"
        );
    }

    check(
        receiver.consumeMessage(),
        "Message consumption failed"
    );

    check(
        !receiver.hasMessage(),
        "Consumed message is still available"
    );
}

// Sends a typed message and verifies its binary content.
template<typename Payload>
static void sendAndExpect(
    MessageIOTestConnection& connection,
    MessageReceiver& receiver,
    const Payload& payload,
    MessageType expectedType,
    const uint8_t* expectedData,
    std::size_t expectedSize
)
{
    std::size_t bytesSent = 0;

    checkResult(
        MessageIO::sendMessage(
            connection.sender,
            payload,
            bytesSent,
            3000
        ),
        "Protocol message sending failed"
    );

    check(
        bytesSent ==
        Constants::HEADER_SIZE + expectedSize,
        "Incorrect number of sent message bytes"
    );

    expectMessage(
        receiver,
        connection.receiver,
        expectedType,
        expectedData,
        expectedSize
    );
}

// Tests all seven supported message types.
static void testAllMessageTypes()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    const uint8_t data[] = { 10, 0, 255 };

    // REGISTER_UR
    RegisterUrPayload userRegistration;

    sendAndExpect(
        connection,
        receiver,
        userRegistration,
        MessageType::REGISTER_UR,
        nullptr,
        0
    );

    // REGISTER_WR
    RegisterWrPayload workerRegistration;

    workerRegistration.workerId =
        0x0102030405060708ULL;

    const uint8_t expectedRegistration[] =
    {
        1, 2, 3, 4, 5, 6, 7, 8
    };

    sendAndExpect(
        connection,
        receiver,
        workerRegistration,
        MessageType::REGISTER_WR,
        expectedRegistration,
        sizeof(expectedRegistration)
    );

    // REQUEST
    RequestPayload request;

    request.clientRequestId = 1;
    request.priority = Priority::HIGH;
    request.dataSize =
        static_cast<uint32_t>(sizeof(data));
    request.data = data;

    const uint8_t expectedRequest[] =
    {
        0, 0, 0, 0, 0, 0, 0, 1,
        static_cast<uint8_t>(Priority::HIGH),
        10, 0, 255
    };

    sendAndExpect(
        connection,
        receiver,
        request,
        MessageType::REQUEST,
        expectedRequest,
        sizeof(expectedRequest)
    );

    // ASSIGN
    AssignPayload assignment;

    assignment.lbRequestId = 100;
    assignment.attemptId = 2;
    assignment.priority = Priority::MEDIUM;
    assignment.dataSize =
        static_cast<uint32_t>(sizeof(data));
    assignment.data = data;

    const uint8_t expectedAssignment[] =
    {
        0, 0, 0, 0, 0, 0, 0, 100,
        0, 0, 0, 0, 0, 0, 0, 2,
        static_cast<uint8_t>(Priority::MEDIUM),
        10, 0, 255
    };

    sendAndExpect(
        connection,
        receiver,
        assignment,
        MessageType::ASSIGN,
        expectedAssignment,
        sizeof(expectedAssignment)
    );

    // HEARTBEAT
    HeartbeatPayload heartbeat;

    sendAndExpect(
        connection,
        receiver,
        heartbeat,
        MessageType::HEARTBEAT,
        nullptr,
        0
    );

    // RESULT
    ResultPayload result;

    result.lbRequestId = 100;
    result.attemptId = 2;
    result.resultSize =
        static_cast<uint32_t>(sizeof(data));
    result.result = data;

    const uint8_t expectedResult[] =
    {
        0, 0, 0, 0, 0, 0, 0, 100,
        0, 0, 0, 0, 0, 0, 0, 2,
        10, 0, 255
    };

    sendAndExpect(
        connection,
        receiver,
        result,
        MessageType::RESULT,
        expectedResult,
        sizeof(expectedResult)
    );

    // RESPONSE
    ResponsePayload response;

    response.clientRequestId = 1;
    response.status = ResponseStatus::SUCCESS;
    response.resultSize =
        static_cast<uint32_t>(sizeof(data));
    response.result = data;

    const uint8_t expectedResponse[] =
    {
        0, 0, 0, 0, 0, 0, 0, 1,
        static_cast<uint8_t>(ResponseStatus::SUCCESS),
        10, 0, 255
    };

    sendAndExpect(
        connection,
        receiver,
        response,
        MessageType::RESPONSE,
        expectedResponse,
        sizeof(expectedResponse)
    );

    closeConnection(connection);

    std::cout
        << "[PASS] All seven network message types test\n";
}

// Tests sending a cancelled response.
static void testCancelledResponse()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    ResponsePayload response;

    response.clientRequestId = 7;
    response.status = ResponseStatus::CANCELLED;

    const uint8_t expected[] =
    {
        0, 0, 0, 0, 0, 0, 0, 7,
        static_cast<uint8_t>(
            ResponseStatus::CANCELLED
        )
    };

    sendAndExpect(
        connection,
        receiver,
        response,
        MessageType::RESPONSE,
        expected,
        sizeof(expected)
    );

    closeConnection(connection);

    std::cout
        << "[PASS] Cancelled response network test\n";
}

// Tests reception when no data is available.
static void testWouldBlock()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    int socketError = 99;

    check(
        receiver.receive(
            connection.receiver,
            socketError
        ) == ReceiveStatus::WouldBlock,
        "Empty connection did not return WouldBlock"
    );

    check(
        socketError == 0,
        "WouldBlock reported a terminal socket error"
    );

    check(
        !receiver.hasMessage(),
        "Empty connection produced a message"
    );

    check(
        !receiver.consumeMessage(),
        "Empty receiver consumed a message"
    );

    closeConnection(connection);

    std::cout << "[PASS] Non-blocking reception test\n";
}

// Tests invalid sending parameters and payload fields.
static void testInvalidSending()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    HeartbeatPayload heartbeat;
    RequestPayload request;

    std::size_t bytesSent = 99;

    check(
        MessageIO::sendMessage(
            INVALID_SOCKET,
            heartbeat,
            bytesSent
        ) == WSAENOTSOCK,
        "Invalid sending socket was accepted"
    );

    check(
        bytesSent == 0,
        "Invalid socket reported sent bytes"
    );

    check(
        MessageIO::sendMessage(
            connection.sender,
            heartbeat,
            bytesSent,
            0
        ) == WSAEINVAL,
        "Invalid sending timeout was accepted"
    );

    request.priority =
        static_cast<Priority>(255);

    check(
        MessageIO::sendMessage(
            connection.sender,
            request,
            bytesSent
        ) == WSAEINVAL,
        "Invalid request priority was accepted"
    );

    check(
        bytesSent == 0,
        "Invalid request reported sent bytes"
    );

    request.priority = Priority::HIGH;
    request.dataSize = 1;
    request.data = nullptr;

    check(
        MessageIO::sendMessage(
            connection.sender,
            request,
            bytesSent
        ) == WSAEINVAL,
        "Invalid request data was accepted"
    );

    int socketError = 0;

    check(
        receiver.receive(
            connection.receiver,
            socketError
        ) == ReceiveStatus::WouldBlock,
        "Invalid sending produced network data"
    );

    // Verify that valid sending still works.
    sendAndExpect(
        connection,
        receiver,
        heartbeat,
        MessageType::HEARTBEAT,
        nullptr,
        0
    );

    closeConnection(connection);

    std::cout << "[PASS] Invalid message sending test\n";
}

// Tests reception of fragmented headers and payloads.
static void testFragmentedMessage()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    const uint8_t payload[] =
    {
        0, 0, 0, 0, 0, 0, 0, 15,
        static_cast<uint8_t>(Priority::HIGH),
        10, 20, 30
    };

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildMessage(
            MessageType::REQUEST,
            payload,
            static_cast<uint32_t>(sizeof(payload)),
            message,
            sizeof(message)
        );

    int socketError = 0;

    // Send only three header bytes.
    sendRaw(connection.sender, message, 3);

    waitReadable(connection.receiver);

    check(
        receiver.receive(
            connection.receiver,
            socketError
        ) == ReceiveStatus::WouldBlock,
        "Partial header produced a complete message"
    );

    check(
        !receiver.hasMessage(),
        "Partial header was accepted"
    );

    // Send the rest of the header and part of the payload.
    sendRaw(
        connection.sender,
        message + 3,
        8
    );

    waitReadable(connection.receiver);

    check(
        receiver.receive(
            connection.receiver,
            socketError
        ) == ReceiveStatus::WouldBlock,
        "Partial payload produced a complete message"
    );

    // Send the remaining payload bytes.
    sendRaw(
        connection.sender,
        message + 11,
        messageSize - 11
    );

    check(
        waitForReceive(
            receiver,
            connection.receiver,
            socketError
        ) == ReceiveStatus::MessageReady,
        "Fragmented message was not completed"
    );

    MessageHeader header = {};
    const uint8_t* receivedPayload = nullptr;

    check(
        receiver.getMessage(
            header, receivedPayload),
        "Fragmented message retrieval failed"
    );

    check(
        header.type == MessageType::REQUEST,
        "Incorrect fragmented message type"
    );

    RequestPayload decoded;

    check(
        PayloadCodec::deserializeRequest(
            receivedPayload,
            header.length,
            decoded
        ),
        "Fragmented request decoding failed"
    );

    check(
        decoded.clientRequestId == 15,
        "Incorrect fragmented request ID"
    );

    check(
        decoded.priority == Priority::HIGH,
        "Incorrect fragmented request priority"
    );

    check(
        decoded.dataSize == 3,
        "Incorrect fragmented data size"
    );

    check(
        decoded.data[0] == 10 &&
        decoded.data[1] == 20 &&
        decoded.data[2] == 30,
        "Incorrect fragmented request data"
    );

    check(
        receiver.consumeMessage(),
        "Fragmented message consumption failed"
    );

    closeConnection(connection);

    std::cout << "[PASS] Fragmented TCP message test\n";
}

// Tests several messages sent in one TCP data buffer.
static void testMultipleMessages()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    uint8_t messages[128] = {};

    const uint8_t workerPayload[] =
    {
        0, 0, 0, 0, 0, 0, 0, 25
    };

    std::size_t totalSize = 0;

    totalSize += buildMessage(
        MessageType::HEARTBEAT,
        nullptr,
        0,
        messages + totalSize,
        sizeof(messages) - totalSize
    );

    totalSize += buildMessage(
        MessageType::REGISTER_WR,
        workerPayload,
        static_cast<uint32_t>(sizeof(workerPayload)),
        messages + totalSize,
        sizeof(messages) - totalSize
    );

    totalSize += buildMessage(
        MessageType::HEARTBEAT,
        nullptr,
        0,
        messages + totalSize,
        sizeof(messages) - totalSize
    );

    sendRaw(
        connection.sender,
        messages,
        totalSize
    );

    expectMessage(
        receiver,
        connection.receiver,
        MessageType::HEARTBEAT,
        nullptr,
        0
    );

    expectMessage(
        receiver,
        connection.receiver,
        MessageType::REGISTER_WR,
        workerPayload,
        sizeof(workerPayload)
    );

    expectMessage(
        receiver,
        connection.receiver,
        MessageType::HEARTBEAT,
        nullptr,
        0
    );

    int socketError = 0;

    check(
        receiver.receive(
            connection.receiver,
            socketError
        ) == ReceiveStatus::WouldBlock,
        "Unexpected additional message was received"
    );

    closeConnection(connection);

    std::cout
        << "[PASS] Multiple TCP messages test\n";
}

// Tests protection of an unread complete message.
static void testUnreadMessageProtection()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    RegisterWrPayload registration;
    registration.workerId = 50;

    HeartbeatPayload heartbeat;

    std::size_t bytesSent = 0;

    checkResult(
        MessageIO::sendMessage(
            connection.sender,
            registration,
            bytesSent
        ),
        "Registration sending failed"
    );

    checkResult(
        MessageIO::sendMessage(
            connection.sender,
            heartbeat,
            bytesSent
        ),
        "Heartbeat sending failed"
    );

    int socketError = 0;

    check(
        waitForReceive(
            receiver,
            connection.receiver,
            socketError
        ) == ReceiveStatus::MessageReady,
        "First message reception failed"
    );

    check(
        receiver.receive(
            connection.receiver,
            socketError
        ) == ReceiveStatus::MessageReady,
        "Unread message state changed"
    );

    const uint8_t expected[] =
    {
        0, 0, 0, 0, 0, 0, 0, 50
    };

    expectMessage(
        receiver,
        connection.receiver,
        MessageType::REGISTER_WR,
        expected,
        sizeof(expected)
    );

    expectMessage(
        receiver,
        connection.receiver,
        MessageType::HEARTBEAT,
        nullptr,
        0
    );

    closeConnection(connection);

    std::cout
        << "[PASS] Unread network message protection test\n";
}

// Tests reception of a message larger than the receive chunk.
static void testLargeMessage()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    constexpr uint32_t DATA_SIZE = 8192;

    static uint8_t data[DATA_SIZE];

    for (uint32_t i = 0; i < DATA_SIZE; ++i)
    {
        data[i] =
            static_cast<uint8_t>(i % 256);
    }

    RequestPayload request;

    request.clientRequestId = 12345;
    request.priority = Priority::LOW;
    request.dataSize = DATA_SIZE;
    request.data = data;

    std::size_t bytesSent = 0;

    checkResult(
        MessageIO::sendMessage(
            connection.sender,
            request,
            bytesSent,
            3000
        ),
        "Large message sending failed"
    );

    check(
        bytesSent ==
        Constants::HEADER_SIZE +
        Constants::REQUEST_FIXED_SIZE +
        DATA_SIZE,
        "Incorrect large message size"
    );

    int socketError = 0;

    check(
        waitForReceive(
            receiver,
            connection.receiver,
            socketError
        ) == ReceiveStatus::MessageReady,
        "Large message reception failed"
    );

    MessageHeader header = {};
    const uint8_t* payload = nullptr;

    check(
        receiver.getMessage(header, payload),
        "Large message retrieval failed"
    );

    check(
        header.type == MessageType::REQUEST,
        "Incorrect large message type"
    );

    RequestPayload decoded;

    check(
        PayloadCodec::deserializeRequest(
            payload,
            header.length,
            decoded
        ),
        "Large request decoding failed"
    );

    check(
        decoded.clientRequestId == request.clientRequestId,
        "Incorrect large request ID"
    );

    check(
        decoded.priority == request.priority,
        "Incorrect large request priority"
    );

    check(
        decoded.dataSize == DATA_SIZE,
        "Incorrect large request data size"
    );

    check(
        std::memcmp(
            decoded.data,
            data,
            DATA_SIZE
        ) == 0,
        "Large request data mismatch"
    );

    check(
        receiver.consumeMessage(),
        "Large message consumption failed"
    );

    closeConnection(connection);

    std::cout << "[PASS] Large TCP message test\n";
}

// Tests an orderly end of the remote TCP sending direction.
static void MessageIOTestConnectionClosed()
{
    MessageIOTestConnection connection;
    MessageReceiver receiver;

    createConnection(connection);

    HeartbeatPayload heartbeat;

    std::size_t bytesSent = 0;

    checkResult(
        MessageIO::sendMessage(
            connection.sender,
            heartbeat,
            bytesSent
        ),
        "Heartbeat sending failed"
    );

    const int shutdownResult =
        shutdown(connection.sender, SD_SEND);

    if (shutdownResult == SOCKET_ERROR)
    {
        checkResult(
            WSAGetLastError(),
            "Sender shutdown failed"
        );
    }

    // The final complete message must not be lost.
    expectMessage(
        receiver,
        connection.receiver,
        MessageType::HEARTBEAT,
        nullptr,
        0
    );

    int socketError = 99;

    check(
        waitForReceive(
            receiver,
            connection.receiver,
            socketError
        ) == ReceiveStatus::ConnectionClosed,
        "Closed connection was not detected"
    );

    check(
        socketError == 0,
        "Orderly shutdown reported a socket error"
    );

    closeConnection(connection);

    std::cout
        << "[PASS] Orderly connection closure test\n";
}

// Tests connection closure during an incomplete message.
static void testIncompleteMessage()
{
    const uint8_t payload[] =
    {
        0, 0, 0, 0, 0, 0, 0, 1,
        static_cast<uint8_t>(Priority::HIGH),
        10, 20, 30
    };

    uint8_t message[64] = {};

    const std::size_t messageSize =
        buildMessage(
            MessageType::REQUEST,
            payload,
            static_cast<uint32_t>(sizeof(payload)),
            message,
            sizeof(message)
        );

    const std::size_t partialSizes[] =
    {
        3,
        messageSize - 1
    };

    // Test both incomplete header and incomplete payload.
    for (int i = 0; i < 2; ++i)
    {
        MessageIOTestConnection connection;
        MessageReceiver receiver;

        createConnection(connection);

        sendRaw(
            connection.sender,
            message,
            partialSizes[i]
        );

        const int shutdownResult =
            shutdown(connection.sender, SD_SEND);

        if (shutdownResult == SOCKET_ERROR)
        {
            checkResult(
                WSAGetLastError(),
                "Sender shutdown failed"
            );
        }

        int socketError = 0;

        check(
            waitForReceive(
                receiver,
                connection.receiver,
                socketError
            ) == ReceiveStatus::IncompleteMessage,
            "Incomplete message was not detected"
        );

        check(
            !receiver.hasMessage(),
            "Incomplete message was accepted"
        );

        closeConnection(connection);
    }

    std::cout
        << "[PASS] Incomplete message on closure test\n";
}

// Tests malformed headers and payloads received over TCP.
static void testInvalidMessages()
{
    MessageReceiver receiver;

    {
        MessageIOTestConnection connection;

        createConnection(connection);

        uint8_t message[Constants::HEADER_SIZE] = {};

        const std::size_t messageSize =
            buildMessage(
                MessageType::HEARTBEAT,
                nullptr,
                0,
                message,
                sizeof(message)
            );

        // Replace the protocol version with an invalid value.
        message[6] = 255;
        message[7] = 255;

        sendRaw(
            connection.sender,
            message,
            messageSize
        );

        int socketError = 0;

        check(
            waitForReceive(
                receiver,
                connection.receiver,
                socketError
            ) == ReceiveStatus::InvalidMessage,
            "Invalid protocol header was accepted"
        );

        check(
            !receiver.hasMessage(),
            "Invalid header produced a message"
        );

        closeConnection(connection);
    }

    // Reset before using the receiver on a new TCP connection.
    receiver.reset();

    {
        MessageIOTestConnection connection;

        createConnection(connection);

        uint8_t payload[
            Constants::REQUEST_FIXED_SIZE
        ] = {};

            // Set an unsupported request priority.
            payload[8] = 255;

            uint8_t message[64] = {};

            const std::size_t messageSize =
                buildMessage(
                    MessageType::REQUEST,
                    payload,
                    sizeof(payload),
                    message,
                    sizeof(message)
                );

            sendRaw(
                connection.sender,
                message,
                messageSize
            );

            int socketError = 0;

            check(
                waitForReceive(
                    receiver,
                    connection.receiver,
                    socketError
                ) == ReceiveStatus::InvalidMessage,
                "Invalid request payload was accepted"
            );

            check(
                receiver.receive(
                    connection.receiver,
                    socketError
                ) == ReceiveStatus::InvalidMessage,
                "Protocol error state was not preserved"
            );

            closeConnection(connection);
    }

    receiver.reset();

    {
        MessageIOTestConnection connection;

        createConnection(connection);

        HeartbeatPayload heartbeat;

        sendAndExpect(
            connection,
            receiver,
            heartbeat,
            MessageType::HEARTBEAT,
            nullptr,
            0
        );

        closeConnection(connection);
    }

    std::cout
        << "[PASS] Invalid network messages and reset test\n";
}

// Tests reception using an invalid socket handle.
static void testInvalidSocket()
{
    MessageReceiver receiver;

    int socketError = 0;

    check(
        receiver.receive(
            INVALID_SOCKET,
            socketError
        ) == ReceiveStatus::SocketError,
        "Invalid receive socket was accepted"
    );

    check(
        socketError == WSAENOTSOCK,
        "Incorrect invalid socket error code"
    );

    check(
        !receiver.hasMessage(),
        "Invalid socket produced a message"
    );

    std::cout << "[PASS] Invalid receive socket test\n";
}

// Runs all network message input and output tests.
void runMessageIOTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for message tests failed"
    );

    testAllMessageTypes();
    testCancelledResponse();
    testWouldBlock();
    testInvalidSending();
    testFragmentedMessage();
    testMultipleMessages();
    testUnreadMessageProtection();
    testLargeMessage();
    MessageIOTestConnectionClosed();
    testIncompleteMessage();
    testInvalidMessages();
    testInvalidSocket();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after message tests failed"
    );

    std::cout << "All message IO tests passed.\n";
}