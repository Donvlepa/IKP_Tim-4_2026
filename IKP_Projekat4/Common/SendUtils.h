#pragma once

#include <winsock2.h>
#include <cstdint>
#include <cstddef>

namespace SendUtils
{
    // Sends all bytes through a connected non-blocking TCP socket.
    //
    // The caller must prevent concurrent send operations
    // on the same socket.
    //
    // Returns zero on success or a Winsock error code.
    // bytesSent contains the number of successfully submitted bytes.
    //
    // timeoutMs must be greater than zero.
    // The function does not change the socket mode.
    //
    // If a partial message is sent before an error,
    // the caller must not resend the complete message
    // through the same connection.
    int sendAll(
        SOCKET socketHandle,
        const uint8_t* data,
        std::size_t dataSize,
        std::size_t& bytesSent,
        uint32_t timeoutMs = 2000
    );
}