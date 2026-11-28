#include "pch.h"

#include <winsock2.h>
#include <Windows.h>

#include "SendUtils.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    // Limits the amount of data submitted by one send call.
    constexpr std::size_t SEND_CHUNK_SIZE = 16 * 1024;

    // Waits until the socket becomes writable.
    int waitForWritableSocket(
        SOCKET socketHandle,
        uint32_t timeoutMs
    )
    {
        fd_set writeSet;

        FD_ZERO(&writeSet);
        FD_SET(socketHandle, &writeSet);

        timeval timeout = {};

        timeout.tv_sec =
            static_cast<long>(timeoutMs / 1000);

        timeout.tv_usec =
            static_cast<long>((timeoutMs % 1000) * 1000);

        const int result = select(
            0,
            nullptr,
            &writeSet,
            nullptr,
            &timeout
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        if (result == 0)
        {
            return WSAETIMEDOUT;
        }

        if (!FD_ISSET(socketHandle, &writeSet))
        {
            return WSAEINVAL;
        }

        return 0;
    }
}

namespace SendUtils
{
    // Sends the complete data buffer through a TCP socket.
    int sendAll(
        SOCKET socketHandle,
        const uint8_t* data,
        std::size_t dataSize,
        std::size_t& bytesSent,
        uint32_t timeoutMs
    )
    {
        bytesSent = 0;

        if (socketHandle == INVALID_SOCKET)
        {
            return WSAENOTSOCK;
        }

        if (data == nullptr && dataSize > 0)
        {
            return WSAEINVAL;
        }

        if (timeoutMs == 0)
        {
            return WSAEINVAL;
        }

        // Sending an empty buffer requires no socket operation.
        if (dataSize == 0)
        {
            return 0;
        }

        const ULONGLONG startTime =
            GetTickCount64();

        while (bytesSent < dataSize)
        {
            // Check the total elapsed sending time.
            const ULONGLONG elapsedTime =
                GetTickCount64() - startTime;

            if (elapsedTime >= timeoutMs)
            {
                return WSAETIMEDOUT;
            }

            const std::size_t remainingBytes =
                dataSize - bytesSent;

            const int bytesToSend =
                static_cast<int>(
                    remainingBytes > SEND_CHUNK_SIZE
                    ? SEND_CHUNK_SIZE
                    : remainingBytes
                    );

            const int result = send(
                socketHandle,
                reinterpret_cast<const char*>(
                    data + bytesSent
                    ),
                bytesToSend,
                0
            );

            if (result > 0)
            {
                bytesSent +=
                    static_cast<std::size_t>(result);

                continue;
            }

            if (result == 0)
            {
                // No progress was made.
                return WSAECONNABORTED;
            }

            const int error = WSAGetLastError();

            // Other socket errors cannot be recovered here.
            if (error != WSAEWOULDBLOCK)
            {
                return error;
            }

            // Recalculate the remaining timeout before waiting.
            const ULONGLONG currentElapsedTime =
                GetTickCount64() - startTime;

            if (currentElapsedTime >= timeoutMs)
            {
                return WSAETIMEDOUT;
            }

            const uint32_t remainingTimeout =
                timeoutMs -
                static_cast<uint32_t>(currentElapsedTime);

            // Wait without continuously polling the socket.
            const int waitResult =
                waitForWritableSocket(
                    socketHandle,
                    remainingTimeout
                );

            if (waitResult != 0)
            {
                return waitResult;
            }
        }

        return 0;
    }
}