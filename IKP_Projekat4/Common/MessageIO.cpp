#include "pch.h"

#include <winsock2.h>

#include "MessageIO.h"
#include "Protocol.h"
#include "PayloadCodec.h"
#include "SendUtils.h"
#include "Constants.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    // Serializes and sends a complete protocol message.
    template<typename Payload>
    int sendEncodedMessage(
        SOCKET socketHandle,
        MessageType type,
        const Payload& payload,
        bool (*serializer)(
            const Payload&,
            uint8_t*,
            std::size_t,
            std::size_t&
            ),
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        bytesSent = 0;

        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        if (timeoutMs == 0)
        {
            return WSAEINVAL;
        }

        uint8_t message[Constants::MAX_MESSAGE_SIZE];

        std::size_t payloadSize = 0;

        // Serialize the payload after the header area.
        if (!serializer(
            payload,
            message + Constants::HEADER_SIZE,
            Constants::MAX_PAYLOAD_SIZE,
            payloadSize))
        {
            return WSAEINVAL;
        }

        if (payloadSize > Constants::MAX_PAYLOAD_SIZE)
        {
            return WSAEMSGSIZE;
        }

        // Prepare the protocol header.
        MessageHeader header = {};

        header.length =
            static_cast<uint32_t>(payloadSize);

        header.type = type;

        header.version =
            Constants::PROTOCOL_VERSION;

        // Serialize the header at the start of the message.
        if (!Protocol::serializeHeader(
            header,
            message,
            Constants::HEADER_SIZE))
        {
            return WSAEINVAL;
        }

        const std::size_t messageSize =
            Constants::HEADER_SIZE + payloadSize;

        // Send the complete header and payload.
        return SendUtils::sendAll(
            socketHandle,
            message,
            messageSize,
            bytesSent,
            timeoutMs
        );
    }
}

namespace MessageIO
{
    // Sends a user registration message.
    int sendMessage(
        SOCKET socketHandle,
        const RegisterUrPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::REGISTER_UR,
            payload,
            PayloadCodec::serializeRegisterUr,
            bytesSent,
            timeoutMs
        );
    }

    // Sends a worker registration message.
    int sendMessage(
        SOCKET socketHandle,
        const RegisterWrPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::REGISTER_WR,
            payload,
            PayloadCodec::serializeRegisterWr,
            bytesSent,
            timeoutMs
        );
    }

    // Sends a user request message.
    int sendMessage(
        SOCKET socketHandle,
        const RequestPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::REQUEST,
            payload,
            PayloadCodec::serializeRequest,
            bytesSent,
            timeoutMs
        );
    }

    // Sends a worker assignment message.
    int sendMessage(
        SOCKET socketHandle,
        const AssignPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::ASSIGN,
            payload,
            PayloadCodec::serializeAssign,
            bytesSent,
            timeoutMs
        );
    }

    // Sends a heartbeat message.
    int sendMessage(
        SOCKET socketHandle,
        const HeartbeatPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::HEARTBEAT,
            payload,
            PayloadCodec::serializeHeartbeat,
            bytesSent,
            timeoutMs
        );
    }

    // Sends a worker result message.
    int sendMessage(
        SOCKET socketHandle,
        const ResultPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::RESULT,
            payload,
            PayloadCodec::serializeResult,
            bytesSent,
            timeoutMs
        );
    }

    // Sends a final response message.
    int sendMessage(
        SOCKET socketHandle,
        const ResponsePayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        return sendEncodedMessage(
            socketHandle,
            MessageType::RESPONSE,
            payload,
            PayloadCodec::serializeResponse,
            bytesSent,
            timeoutMs
        );
    }
}

// Initializes the message receiver.
MessageReceiver::MessageReceiver()
    : pendingOffset_(0),
    pendingSize_(0)
{
}

// Receives and extracts one complete protocol message.
ReceiveStatus MessageReceiver::receive(
    SOCKET socketHandle,
    int& socketError
)
{
    socketError = 0;

    if (socketHandle == INVALID_SOCKET)
    {
        socketError = WSAENOTSOCK;

        return ReceiveStatus::SocketError;
    }

    if (messageBuffer_.hasError())
    {
        return ReceiveStatus::InvalidMessage;
    }

    // Do not overwrite a message that has not been consumed.
    if (messageBuffer_.hasMessage())
    {
        return ReceiveStatus::MessageReady;
    }

    while (true)
    {
        // Process previously received bytes first.
        if (pendingOffset_ < pendingSize_)
        {
            std::size_t consumed = 0;

            const MessageBufferStatus status =
                messageBuffer_.append(
                    receiveBuffer_ + pendingOffset_,
                    pendingSize_ - pendingOffset_,
                    consumed
                );

            pendingOffset_ += consumed;

            if (status ==
                MessageBufferStatus::InvalidMessage)
            {
                return ReceiveStatus::InvalidMessage;
            }

            if (status ==
                MessageBufferStatus::InvalidInput)
            {
                socketError = WSAEINVAL;

                return ReceiveStatus::SocketError;
            }

            if (status ==
                MessageBufferStatus::MessageReady)
            {
                return ReceiveStatus::MessageReady;
            }

            // Prevent an infinite loop if no progress is made.
            if (consumed == 0)
            {
                socketError = WSAEINVAL;

                return ReceiveStatus::SocketError;
            }

            continue;
        }

        // All previously received bytes have been processed.
        pendingOffset_ = 0;
        pendingSize_ = 0;

        // Receive the next chunk of TCP data.
        const int result = recv(
            socketHandle,
            reinterpret_cast<char*>(receiveBuffer_),
            static_cast<int>(RECEIVE_CHUNK_SIZE),
            0
        );

        if (result > 0)
        {
            pendingSize_ =
                static_cast<std::size_t>(result);

            continue;
        }

        // The remote peer has closed its sending direction.
        if (result == 0)
        {
            if (messageBuffer_.getBufferedSize() > 0)
            {
                return ReceiveStatus::IncompleteMessage;
            }

            return ReceiveStatus::ConnectionClosed;
        }

        const int error = WSAGetLastError();

        // No data is currently available.
        if (error == WSAEWOULDBLOCK)
        {
            return ReceiveStatus::WouldBlock;
        }

        socketError = error;

        return ReceiveStatus::SocketError;
    }
}

// Checks whether a complete message is available.
bool MessageReceiver::hasMessage() const
{
    return messageBuffer_.hasMessage();
}

// Returns the current message without consuming it.
bool MessageReceiver::getMessage(
    MessageHeader& header,
    const uint8_t*& payload
) const
{
    return messageBuffer_.getMessage(
        header,
        payload
    );
}

// Consumes the current message.
bool MessageReceiver::consumeMessage()
{
    return messageBuffer_.consumeMessage();
}

// Clears both the message buffer and pending TCP bytes.
void MessageReceiver::reset()
{
    messageBuffer_.reset();

    pendingOffset_ = 0;
    pendingSize_ = 0;
}