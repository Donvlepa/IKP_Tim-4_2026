#pragma once

#include <winsock2.h>
#include <cstdint>
#include <cstddef>

#include "Payloads.h"
#include "MessageBuffer.h"

namespace MessageIO
{
    // Sends a complete protocol message.
    // The socket must be connected and non-blocking.
    // The caller must synchronize concurrent send operations.
    // Returns zero on success or a Winsock error code.
    // bytesSent includes both the header and the payload.

    int sendMessage(
        SOCKET socketHandle,
        const RegisterUrPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );

    int sendMessage(
        SOCKET socketHandle,
        const RegisterWrPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );

    int sendMessage(
        SOCKET socketHandle,
        const RequestPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );

    int sendMessage(
        SOCKET socketHandle,
        const AssignPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );

    int sendMessage(
        SOCKET socketHandle,
        const HeartbeatPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );

    int sendMessage(
        SOCKET socketHandle,
        const ResultPayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );

    int sendMessage(
        SOCKET socketHandle,
        const ResponsePayload& payload,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );
}

// Represents the result of a message reception operation.
enum class ReceiveStatus
{
    MessageReady,
    WouldBlock,
    ConnectionClosed,
    IncompleteMessage,
    InvalidMessage,
    SocketError
};

// Receives complete protocol messages from one TCP connection.
class MessageReceiver
{
public:
    MessageReceiver();

    MessageReceiver(const MessageReceiver&) = delete;

    MessageReceiver& operator=(
        const MessageReceiver&
        ) = delete;

    // Receives data from a connected non-blocking socket.
    // Returns when a message is ready, no data is available,
    // the connection is closed, or an error occurs.
    //
    // Only SocketError sets a non-zero socketError value.
    // The caller must close the connection on terminal errors.
    ReceiveStatus receive(
        SOCKET socketHandle,
        int& socketError
    );

    // Checks whether a complete message is available.
    bool hasMessage() const;

    // Returns the decoded header and payload view.
    // The payload remains valid until consumeMessage()
    // or reset() is called.
    bool getMessage(
        MessageHeader& header,
        const uint8_t*& payload
    ) const;

    // Consumes the current message without losing pending bytes.
    bool consumeMessage();

    // Clears all reception state.
    // Use only when discarding the old TCP stream.
    void reset();

private:
    static constexpr std::size_t RECEIVE_CHUNK_SIZE = 4096;

    MessageBuffer messageBuffer_;

    uint8_t receiveBuffer_[RECEIVE_CHUNK_SIZE];

    std::size_t pendingOffset_;
    std::size_t pendingSize_;
};