#include <winsock2.h>

#include <iostream>
#include <cstdlib>
#include <type_traits>

#include "WinsockContext.h"
#include "SocketUtils.h"
#include "TcpListener.h"
#include "TestSuites.h"

namespace
{
    constexpr const char* LOCAL_ADDRESS =
        "127.0.0.1";

    // Verifies that listener ownership cannot be copied.
    static_assert(
        !std::is_copy_constructible<TcpListener>::value,
        "TcpListener must not be copy constructible"
        );

    static_assert(
        !std::is_copy_assignable<TcpListener>::value,
        "TcpListener must not be copy assignable"
        );

    static_assert(
        !std::is_move_constructible<TcpListener>::value,
        "TcpListener must not be move constructible"
        );

    static_assert(
        !std::is_move_assignable<TcpListener>::value,
        "TcpListener must not be move assignable"
        );

    // Checks a test condition.
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr
                << "[FAIL] " << message << "\n";

            std::abort();
        }
    }

    // Checks a Winsock operation result.
    void checkResult(
        int result,
        const char* message
    )
    {
        if (result != 0)
        {
            std::cerr
                << "[FAIL] " << message
                << " | Winsock error: "
                << result << "\n";

            std::abort();
        }
    }

    // Verifies that a listener is inactive.
    void checkInactive(
        const TcpListener& listener
    )
    {
        check(
            !listener.isListening(),
            "Listener is still active"
        );

        check(
            listener.getSocket() == INVALID_SOCKET,
            "Inactive listener contains a socket"
        );

        check(
            listener.getPort() == 0,
            "Inactive listener contains a port"
        );
    }

    // Tests the initial listener state.
    void testInitialState()
    {
        TcpListener listener;

        checkInactive(listener);

        checkResult(
            listener.stop(),
            "Inactive listener cleanup failed"
        );

        checkInactive(listener);

        std::cout
            << "[PASS] Initial listener state test\n";
    }

    // Tests successful server startup.
    void testStartListener()
    {
        TcpListener listener;

        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Listener startup failed"
        );

        check(
            listener.isListening(),
            "Listener is not active"
        );

        check(
            listener.getSocket() != INVALID_SOCKET,
            "Listening socket is invalid"
        );

        check(
            listener.getPort() != 0,
            "Assigned listening port is invalid"
        );

        uint16_t actualPort = 0;

        checkResult(
            SocketUtils::getLocalPort(
                listener.getSocket(),
                actualPort),
            "Actual port retrieval failed"
        );

        check(
            actualPort == listener.getPort(),
            "Listening port mismatch"
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        checkInactive(listener);

        std::cout
            << "[PASS] Listener startup test\n";
    }

    // Tests repeated start operations.
    void testDuplicateStart()
    {
        TcpListener listener;

        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Initial listener startup failed"
        );

        const SOCKET originalSocket =
            listener.getSocket();

        const uint16_t originalPort =
            listener.getPort();

        check(
            listener.start(LOCAL_ADDRESS, 0)
            == WSAEINVAL,
            "Duplicate listener startup was accepted"
        );

        check(
            listener.isListening(),
            "Duplicate startup stopped the listener"
        );

        check(
            listener.getSocket() == originalSocket,
            "Duplicate startup replaced the socket"
        );

        check(
            listener.getPort() == originalPort,
            "Duplicate startup changed the port"
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] Duplicate listener startup test\n";
    }

    // Tests invalid startup parameters and recovery.
    void testInvalidParameters()
    {
        TcpListener listener;

        const char* invalidAddresses[] =
        {
            nullptr,
            "",
            "invalid",
            "999.999.999.999"
        };

        for (int i = 0; i < 4; ++i)
        {
            check(
                listener.start(
                    invalidAddresses[i], 0
                ) == WSAEINVAL,
                "Invalid address was accepted"
            );

            checkInactive(listener);
        }

        check(
            listener.start(
                LOCAL_ADDRESS, 0, 0
            ) == WSAEINVAL,
            "Invalid backlog was accepted"
        );

        checkInactive(listener);

        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Listener startup after invalid parameters failed"
        );

        check(
            listener.isListening(),
            "Listener recovery failed"
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] Invalid listener parameters test\n";
    }

    // Tests protection against binding the same endpoint twice.
    void testPortConflict()
    {
        TcpListener firstListener;
        TcpListener secondListener;

        checkResult(
            firstListener.start(
                LOCAL_ADDRESS, 0),
            "First listener startup failed"
        );

        const uint16_t occupiedPort =
            firstListener.getPort();

        const int result =
            secondListener.start(
                LOCAL_ADDRESS,
                occupiedPort
            );

        check(
            result == WSAEADDRINUSE ||
            result == WSAEACCES,
            "Unexpected port conflict result"
        );

        checkInactive(secondListener);

        check(
            firstListener.isListening(),
            "Port conflict stopped the first listener"
        );

        check(
            firstListener.getPort() == occupiedPort,
            "Port conflict changed the first listener"
        );

        // Verify that the failed listener can be reused.
        checkResult(
            secondListener.start(
                LOCAL_ADDRESS, 0),
            "Listener recovery after port conflict failed"
        );

        check(
            secondListener.getPort() != occupiedPort,
            "Both listeners use the same endpoint"
        );

        checkResult(
            secondListener.stop(),
            "Second listener cleanup failed"
        );

        checkResult(
            firstListener.stop(),
            "First listener cleanup failed"
        );

        std::cout
            << "[PASS] Listening port conflict test\n";
    }

    // Tests non-blocking acceptance without pending clients.
    void testNonBlockingListener()
    {
        TcpListener listener;

        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Listener startup failed"
        );

        SOCKET acceptedSocket = INVALID_SOCKET;

        const int result =
            SocketUtils::acceptSocket(
                listener.getSocket(),
                acceptedSocket
            );

        check(
            result == WSAEWOULDBLOCK,
            "Listener is not operating in non-blocking mode"
        );

        check(
            acceptedSocket == INVALID_SOCKET,
            "Empty listener returned a client socket"
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] Non-blocking listener test\n";
    }

    // Tests a real incoming TCP connection.
    void testIncomingConnection()
    {
        TcpListener listener;

        checkResult(
            listener.start(LOCAL_ADDRESS, 0),
            "Listener startup failed"
        );

        SOCKET clientSocket = INVALID_SOCKET;
        SOCKET acceptedSocket = INVALID_SOCKET;

        checkResult(
            SocketUtils::createTcpSocket(
                clientSocket),
            "Client creation failed"
        );

        checkResult(
            SocketUtils::connectSocket(
                clientSocket,
                LOCAL_ADDRESS,
                listener.getPort()),
            "Client connection failed"
        );

        // Wait for the listener to become readable.
        fd_set readSet;

        FD_ZERO(&readSet);

        FD_SET(
            listener.getSocket(),
            &readSet
        );

        timeval timeout = {};

        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        const int selectResult = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout
        );

        check(
            selectResult == 1 &&
            FD_ISSET(
                listener.getSocket(),
                &readSet),
            "Incoming connection was not detected"
        );

        checkResult(
            SocketUtils::acceptSocket(
                listener.getSocket(),
                acceptedSocket),
            "Incoming connection acceptance failed"
        );

        check(
            acceptedSocket != INVALID_SOCKET,
            "Accepted socket is invalid"
        );

        check(
            acceptedSocket != listener.getSocket(),
            "Accepted socket equals listening socket"
        );

        uint16_t acceptedPort = 0;

        checkResult(
            SocketUtils::getLocalPort(
                acceptedSocket,
                acceptedPort),
            "Accepted socket port retrieval failed"
        );

        check(
            acceptedPort == listener.getPort(),
            "Accepted socket local port mismatch"
        );

        checkResult(
            SocketUtils::closeSocket(
                acceptedSocket),
            "Accepted socket cleanup failed"
        );

        checkResult(
            SocketUtils::closeSocket(
                clientSocket),
            "Client socket cleanup failed"
        );

        checkResult(
            listener.stop(),
            "Listener cleanup failed"
        );

        std::cout
            << "[PASS] Incoming TCP connection test\n";
    }

    // Tests stopping and restarting the listener.
    void testListenerRestart()
    {
        TcpListener listener;

        for (int i = 0; i < 100; ++i)
        {
            checkResult(
                listener.start(LOCAL_ADDRESS, 0),
                "Repeated listener startup failed"
            );

            check(
                listener.isListening(),
                "Restarted listener is inactive"
            );

            checkResult(
                listener.stop(),
                "Repeated listener cleanup failed"
            );

            checkInactive(listener);

            checkResult(
                listener.stop(),
                "Duplicate listener cleanup failed"
            );
        }

        std::cout
            << "[PASS] Listener restart test\n";
    }

    // Tests automatic listener cleanup.
    void testDestructorCleanup()
    {
        uint16_t releasedPort = 0;

        {
            TcpListener listener;

            checkResult(
                listener.start(
                    LOCAL_ADDRESS, 0),
                "Listener startup failed"
            );

            releasedPort = listener.getPort();

            check(
                releasedPort != 0,
                "Invalid listener port"
            );

            // The destructor closes the listening socket.
        }

        TcpListener replacement;

        checkResult(
            replacement.start(
                LOCAL_ADDRESS,
                releasedPort),
            "Destructor did not release the listening endpoint"
        );

        check(
            replacement.isListening(),
            "Replacement listener is inactive"
        );

        checkResult(
            replacement.stop(),
            "Replacement listener cleanup failed"
        );

        std::cout
            << "[PASS] Listener destructor cleanup test\n";
    }
}

// Runs all TCP listener tests.
void runTcpListenerTests()
{
    WinsockContext context;

    checkResult(
        context.initialize(),
        "Winsock initialization for listener tests failed"
    );

    testInitialState();
    testStartListener();
    testDuplicateStart();
    testInvalidParameters();
    testPortConflict();
    testNonBlockingListener();
    testIncomingConnection();
    testListenerRestart();
    testDestructorCleanup();

    checkResult(
        context.cleanup(),
        "Winsock cleanup after listener tests failed"
    );

    std::cout
        << "All TCP listener tests passed.\n";
}