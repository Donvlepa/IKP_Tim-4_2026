#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "SendUtils.h"
#include "Constants.h"
#include "TestSuites.h"

// Stores sockets used by a local TCP connection.
struct TestConnection
{
    SOCKET listener = INVALID_SOCKET;
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

// Checks the result of a socket operation.
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

// Configures an integer socket option.
static void setSocketOption(
    SOCKET socketHandle,
    int option,
    int value
)
{
    const int result = setsockopt(
        socketHandle,
        SOL_SOCKET,
        option,
        reinterpret_cast<const char*>(&value),
        static_cast<int>(sizeof(value))
    );

    if (result == SOCKET_ERROR)
    {
        checkResult(
            WSAGetLastError(),
            "Socket option configuration failed"
        );
    }
}

// Creates a connected pair of local TCP sockets.
static void createTestConnection(
    TestConnection& connection,
    bool smallBuffers = false
)
{
    checkResult(
        SocketUtils::createTcpSocket(connection.listener),
        "Listener creation failed"
    );

    if (smallBuffers)
    {
        setSocketOption(
            connection.listener,
            SO_RCVBUF,
            4096
        );
    }

    checkResult(
        SocketUtils::bindSocket(
            connection.listener,
            "127.0.0.1",
            0
        ),
        "Listener binding failed"
    );

    checkResult(
        SocketUtils::listenSocket(connection.listener),
        "Listen operation failed"
    );

    uint16_t port = 0;

    checkResult(
        SocketUtils::getLocalPort(
            connection.listener,
            port
        ),
        "Listener port retrieval failed"
    );

    checkResult(
        SocketUtils::createTcpSocket(connection.sender),
        "Sender creation failed"
    );

    if (smallBuffers)
    {
        setSocketOption(
            connection.sender,
            SO_SNDBUF,
            4096
        );
    }

    checkResult(
        SocketUtils::connectSocket(
            connection.sender,
            "127.0.0.1",
            port
        ),
        "Sender connection failed"
    );

    // Wait for the incoming connection.
    fd_set readSet;

    FD_ZERO(&readSet);
    FD_SET(connection.listener, &readSet);

    timeval timeout = {};
    timeout.tv_sec = 3;

    check(
        select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        ) == 1,
        "Incoming test connection was not detected"
    );

    checkResult(
        SocketUtils::acceptSocket(
            connection.listener,
            connection.receiver
        ),
        "Receiver acceptance failed"
    );

    if (smallBuffers)
    {
        setSocketOption(
            connection.receiver,
            SO_RCVBUF,
            4096
        );
    }

    // sendAll requires a non-blocking sender socket.
    checkResult(
        SocketUtils::setNonBlocking(
            connection.sender,
            true
        ),
        "Enabling non-blocking sending failed"
    );

    // Prevent test receive operations from blocking indefinitely.
    const DWORD receiveTimeout = 5000;

    const int timeoutResult = setsockopt(
        connection.receiver,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&receiveTimeout),
        static_cast<int>(sizeof(receiveTimeout))
    );

    if (timeoutResult == SOCKET_ERROR)
    {
        checkResult(
            WSAGetLastError(),
            "Receive timeout configuration failed"
        );
    }
}

// Releases all sockets used by a test connection.
static void closeTestConnection(
    TestConnection& connection
)
{
    checkResult(
        SocketUtils::closeSocket(connection.sender),
        "Sender cleanup failed"
    );

    checkResult(
        SocketUtils::closeSocket(connection.receiver),
        "Receiver cleanup failed"
    );

    checkResult(
        SocketUtils::closeSocket(connection.listener),
        "Listener cleanup failed"
    );
}

// Receives the requested number of bytes for testing.
static int receiveExact(
    SOCKET socketHandle,
    uint8_t* buffer,
    std::size_t bufferSize
)
{
    std::size_t bytesReceived = 0;

    while (bytesReceived < bufferSize)
    {
        const std::size_t remaining =
            bufferSize - bytesReceived;

        const int bytesToReceive =
            static_cast<int>(
                remaining > 4096
                ? 4096
                : remaining
                );

        const int result = recv(
            socketHandle,
            reinterpret_cast<char*>(
                buffer + bytesReceived
                ),
            bytesToReceive,
            0
        );

        if (result == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }

        if (result == 0)
        {
            return WSAECONNRESET;
        }

        bytesReceived +=
            static_cast<std::size_t>(result);
    }

    return 0;
}

// Contains parameters for the test receiving thread.
struct ReceiveTask
{
    SOCKET socketHandle;
    uint8_t* buffer;
    std::size_t bufferSize;
    int result;
};

// Receives data while the sender is running.
static unsigned __stdcall receiveThread(void* parameter)
{
    ReceiveTask* task =
        static_cast<ReceiveTask*>(parameter);

    task->result = receiveExact(
        task->socketHandle,
        task->buffer,
        task->bufferSize
    );

    return 0;
}

// Tests sending a small data buffer.
static void testSmallBuffer()
{
    TestConnection connection;

    createTestConnection(connection);

    const uint8_t data[] =
    {
        10, 20, 30, 40, 50
    };

    uint8_t received[sizeof(data)] = {};

    std::size_t bytesSent = 0;

    checkResult(
        SendUtils::sendAll(
            connection.sender,
            data,
            sizeof(data),
            bytesSent
        ),
        "Small buffer sending failed"
    );

    check(
        bytesSent == sizeof(data),
        "Incorrect small buffer sent size"
    );

    checkResult(
        receiveExact(
            connection.receiver,
            received,
            sizeof(received)
        ),
        "Small buffer reception failed"
    );

    check(
        std::memcmp(
            data,
            received,
            sizeof(data)
        ) == 0,
        "Small buffer data mismatch"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Small buffer sending test\n";
}

// Tests binary data containing zero and maximum byte values.
static void testBinaryData()
{
    TestConnection connection;

    createTestConnection(connection);

    const uint8_t data[] =
    {
        0, 255, 128, 0, 1, 254, 0, 127
    };

    uint8_t received[sizeof(data)] = {};

    std::size_t bytesSent = 0;

    checkResult(
        SendUtils::sendAll(
            connection.sender,
            data,
            sizeof(data),
            bytesSent
        ),
        "Binary data sending failed"
    );

    check(
        bytesSent == sizeof(data),
        "Incorrect binary data sent size"
    );

    checkResult(
        receiveExact(
            connection.receiver,
            received,
            sizeof(received)
        ),
        "Binary data reception failed"
    );

    check(
        std::memcmp(
            data,
            received,
            sizeof(data)
        ) == 0,
        "Binary data mismatch"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Binary data sending test\n";
}

// Tests sending an empty data buffer.
static void testEmptyBuffer()
{
    TestConnection connection;

    createTestConnection(connection);

    std::size_t bytesSent = 99;

    checkResult(
        SendUtils::sendAll(
            connection.sender,
            nullptr,
            0,
            bytesSent
        ),
        "Empty buffer sending failed"
    );

    check(
        bytesSent == 0,
        "Empty buffer sent unexpected bytes"
    );

    // Verify that no data was submitted to the connection.
    fd_set readSet;

    FD_ZERO(&readSet);
    FD_SET(connection.receiver, &readSet);

    timeval timeout = {};

    check(
        select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        ) == 0,
        "Empty buffer produced incoming data"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Empty buffer sending test\n";
}

// Tests invalid socket and buffer arguments.
static void testInvalidParameters()
{
    TestConnection connection;

    createTestConnection(connection);

    const uint8_t data[] = { 1, 2, 3 };

    std::size_t bytesSent = 99;

    check(
        SendUtils::sendAll(
            INVALID_SOCKET,
            data,
            sizeof(data),
            bytesSent
        ) == WSAENOTSOCK,
        "Invalid socket was accepted"
    );

    check(
        bytesSent == 0,
        "Invalid socket reported sent bytes"
    );

    bytesSent = 99;

    check(
        SendUtils::sendAll(
            connection.sender,
            nullptr,
            sizeof(data),
            bytesSent
        ) == WSAEINVAL,
        "Null non-empty buffer was accepted"
    );

    check(
        bytesSent == 0,
        "Invalid buffer reported sent bytes"
    );

    bytesSent = 99;

    check(
        SendUtils::sendAll(
            connection.sender,
            data,
            sizeof(data),
            bytesSent,
            0
        ) == WSAEINVAL,
        "Zero sending timeout was accepted"
    );

    check(
        bytesSent == 0,
        "Invalid timeout reported sent bytes"
    );

    // Verify that the connection remains usable.
    checkResult(
        SendUtils::sendAll(
            connection.sender,
            data,
            sizeof(data),
            bytesSent
        ),
        "Valid sending failed after invalid parameters"
    );

    uint8_t received[sizeof(data)] = {};

    checkResult(
        receiveExact(
            connection.receiver,
            received,
            sizeof(received)
        ),
        "Reception after invalid parameters failed"
    );

    check(
        std::memcmp(
            data,
            received,
            sizeof(data)
        ) == 0,
        "Data mismatch after invalid parameters"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Invalid send parameters test\n";
}

// Tests consecutive sendAll operations on the same connection.
static void testConsecutiveSends()
{
    TestConnection connection;

    createTestConnection(connection);

    const uint8_t firstData[] =
    {
        1, 2, 3
    };

    const uint8_t secondData[] =
    {
        4, 5, 6, 7
    };

    const uint8_t expected[] =
    {
        1, 2, 3, 4, 5, 6, 7
    };

    uint8_t received[sizeof(expected)] = {};

    std::size_t bytesSent = 0;

    checkResult(
        SendUtils::sendAll(
            connection.sender,
            firstData,
            sizeof(firstData),
            bytesSent
        ),
        "First consecutive send failed"
    );

    check(
        bytesSent == sizeof(firstData),
        "Incorrect first consecutive send size"
    );

    checkResult(
        SendUtils::sendAll(
            connection.sender,
            secondData,
            sizeof(secondData),
            bytesSent
        ),
        "Second consecutive send failed"
    );

    check(
        bytesSent == sizeof(secondData),
        "Incorrect second consecutive send size"
    );

    checkResult(
        receiveExact(
            connection.receiver,
            received,
            sizeof(received)
        ),
        "Consecutive data reception failed"
    );

    check(
        std::memcmp(
            received,
            expected,
            sizeof(expected)
        ) == 0,
        "Consecutive send data order mismatch"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Consecutive sends test\n";
}

// Tests sending a maximum-sized protocol message buffer.
static void testMaximumMessage()
{
    TestConnection connection;

    createTestConnection(connection);

    static uint8_t data[
        Constants::MAX_MESSAGE_SIZE
    ];

    static uint8_t received[
        Constants::MAX_MESSAGE_SIZE
    ];

    for (std::size_t i = 0;
        i < sizeof(data);
        ++i)
    {
        data[i] =
            static_cast<uint8_t>(i % 256);
    }

    std::memset(received, 0, sizeof(received));

    ReceiveTask task =
    {
        connection.receiver,
        received,
        sizeof(received),
        WSAEINVAL
    };

    const uintptr_t threadHandle =
        _beginthreadex(
            nullptr,
            0,
            receiveThread,
            &task,
            0,
            nullptr
        );

    if (threadHandle == 0)
    {
        std::cerr << "[FAIL] Receiver thread creation failed\n";
        std::abort();
    }

    HANDLE receiverThread =
        reinterpret_cast<HANDLE>(threadHandle);

    std::size_t bytesSent = 0;

    const int sendResult = SendUtils::sendAll(
        connection.sender,
        data,
        sizeof(data),
        bytesSent,
        5000
    );

    const DWORD waitResult =
        WaitForSingleObject(
            receiverThread,
            10000
        );

    check(
        waitResult == WAIT_OBJECT_0,
        "Receiver thread did not finish"
    );

    check(
        CloseHandle(receiverThread) != 0,
        "Receiver thread handle cleanup failed"
    );

    checkResult(
        sendResult,
        "Maximum message sending failed"
    );

    checkResult(
        task.result,
        "Maximum message reception failed"
    );

    check(
        bytesSent == sizeof(data),
        "Maximum message was not fully sent"
    );

    check(
        std::memcmp(
            data,
            received,
            sizeof(data)
        ) == 0,
        "Maximum message data mismatch"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Maximum message sending test\n";
}

// Tests sending after the local send direction is shut down.
static void testShutdownSocket()
{
    TestConnection connection;

    createTestConnection(connection);

    const int shutdownResult =
        shutdown(connection.sender, SD_SEND);

    if (shutdownResult == SOCKET_ERROR)
    {
        checkResult(
            WSAGetLastError(),
            "Socket shutdown failed"
        );
    }

    const uint8_t data[] = { 1, 2, 3 };

    std::size_t bytesSent = 99;

    const int result = SendUtils::sendAll(
        connection.sender,
        data,
        sizeof(data),
        bytesSent
    );

    check(
        result == WSAESHUTDOWN,
        "Send after shutdown returned an unexpected result"
    );

    check(
        bytesSent == 0,
        "Shutdown socket reported sent bytes"
    );

    closeTestConnection(connection);

    std::cout << "[PASS] Shutdown socket sending test\n";
}

// Tests timeout when the receiving side does not read data.
static void testSendTimeout()
{
    TestConnection connection;

    createTestConnection(connection, true);

    // Large test data is used to fill the TCP buffers.
    constexpr std::size_t DATA_SIZE =
        8 * 1024 * 1024;

    static uint8_t data[DATA_SIZE] = {};

    constexpr uint32_t TIMEOUT_MS = 1000;

    std::size_t bytesSent = 0;

    const ULONGLONG startTime =
        GetTickCount64();

    // The receiver intentionally does not read any data.
    const int result = SendUtils::sendAll(
        connection.sender,
        data,
        sizeof(data),
        bytesSent,
        TIMEOUT_MS
    );

    const ULONGLONG elapsedTime =
        GetTickCount64() - startTime;

    check(
        result == WSAETIMEDOUT,
        "Blocked sending did not return a timeout"
    );

    check(
        bytesSent > 0,
        "No partial sending progress was recorded"
    );

    check(
        bytesSent < sizeof(data),
        "Timed-out send reported complete data"
    );

    // Allow scheduling and system timer variations.
    check(
        elapsedTime < 5000,
        "Sending timeout took too long"
    );

    std::cout << "       Partial bytes sent: "
        << bytesSent << "/"
        << sizeof(data) << "\n";

    std::cout << "       Elapsed time: "
        << elapsedTime << " ms\n";

    closeTestConnection(connection);

    std::cout << "[PASS] Sending timeout test\n";
}

// Runs all send utility tests.
void runSendUtilsTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for send tests failed"
    );

    testSmallBuffer();
    testBinaryData();
    testEmptyBuffer();
    testInvalidParameters();
    testConsecutiveSends();
    testMaximumMessage();
    testShutdownSocket();
    testSendTimeout();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after send tests failed"
    );

    std::cout << "All send utility tests passed.\n";
}