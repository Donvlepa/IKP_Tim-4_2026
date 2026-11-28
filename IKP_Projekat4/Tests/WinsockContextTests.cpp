#include <winsock2.h>

#include <iostream>
#include <cstdlib>
#include <type_traits>

#include "WinsockContext.h"
#include "TestSuites.h"

// Prevents accidental copying or moving of Winsock ownership.
static_assert(
    !std::is_copy_constructible<WinsockContext>::value,
    "WinsockContext must not be copy constructible"
    );

static_assert(
    !std::is_copy_assignable<WinsockContext>::value,
    "WinsockContext must not be copy assignable"
    );

static_assert(
    !std::is_move_constructible<WinsockContext>::value,
    "WinsockContext must not be move constructible"
    );

static_assert(
    !std::is_move_assignable<WinsockContext>::value,
    "WinsockContext must not be move assignable"
    );

// Checks a test condition.
static void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";

        std::abort();
    }
}

// Checks whether a TCP socket can be created and closed.
static void checkSocketAvailable()
{
    SOCKET testSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    check(
        testSocket != INVALID_SOCKET,
        "TCP socket creation failed"
    );

    check(
        closesocket(testSocket) == 0,
        "TCP socket cleanup failed"
    );
}

// Checks that Winsock is not initialized in the process.
static void checkWinsockInactive()
{
    SOCKET testSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (testSocket != INVALID_SOCKET)
    {
        closesocket(testSocket);

        check(
            false,
            "Winsock is still initialized"
        );
    }

    check(
        WSAGetLastError() == WSANOTINITIALISED,
        "Unexpected socket error"
    );
}

// Tests the initial context state.
static void testInitialState()
{
    WinsockContext context;

    check(
        !context.isInitialized(),
        "New context is already initialized"
    );

    check(
        context.cleanup() == 0,
        "Cleanup of inactive context failed"
    );

    check(
        !context.isInitialized(),
        "Inactive context changed state"
    );

    std::cout << "[PASS] Initial Winsock state test\n";
}

// Tests successful Winsock initialization.
static void testInitialization()
{
    WinsockContext context;

    check(
        context.initialize() == 0,
        "Winsock initialization failed"
    );

    check(
        context.isInitialized(),
        "Context is not initialized"
    );

    checkSocketAvailable();

    check(
        context.cleanup() == 0,
        "Winsock cleanup failed"
    );

    check(
        !context.isInitialized(),
        "Context is still initialized"
    );

    checkWinsockInactive();

    std::cout << "[PASS] Winsock initialization test\n";
}

// Tests repeated initialization of the same context.
static void testRepeatedInitialization()
{
    WinsockContext context;

    check(
        context.initialize() == 0,
        "First initialization failed"
    );

    check(
        context.initialize() == 0,
        "Second initialization failed"
    );

    check(
        context.initialize() == 0,
        "Third initialization failed"
    );

    check(
        context.isInitialized(),
        "Context initialization state is invalid"
    );

    checkSocketAvailable();

    check(
        context.cleanup() == 0,
        "Cleanup after repeated initialization failed"
    );

    checkWinsockInactive();

    std::cout << "[PASS] Repeated initialization test\n";
}

// Tests repeated cleanup operations.
static void testRepeatedCleanup()
{
    WinsockContext context;

    check(
        context.initialize() == 0,
        "Initialization failed"
    );

    check(
        context.cleanup() == 0,
        "First cleanup failed"
    );

    check(
        context.cleanup() == 0,
        "Second cleanup failed"
    );

    check(
        context.cleanup() == 0,
        "Third cleanup failed"
    );

    check(
        !context.isInitialized(),
        "Context is still initialized"
    );

    checkWinsockInactive();

    std::cout << "[PASS] Repeated cleanup test\n";
}

// Tests initialization after resources have been released.
static void testReinitialization()
{
    WinsockContext context;

    for (int i = 0; i < 5; ++i)
    {
        check(
            context.initialize() == 0,
            "Winsock reinitialization failed"
        );

        check(
            context.isInitialized(),
            "Invalid reinitialization state"
        );

        checkSocketAvailable();

        check(
            context.cleanup() == 0,
            "Cleanup after reinitialization failed"
        );

        check(
            !context.isInitialized(),
            "Context remained initialized"
        );

        checkWinsockInactive();
    }

    std::cout << "[PASS] Winsock reinitialization test\n";
}

// Tests automatic resource cleanup by the destructor.
static void testDestructorCleanup()
{
    {
        WinsockContext context;

        check(
            context.initialize() == 0,
            "Initialization inside scope failed"
        );

        check(
            context.isInitialized(),
            "Context inside scope is inactive"
        );

        checkSocketAvailable();

        // The destructor releases Winsock resources.
    }

    checkWinsockInactive();

    std::cout << "[PASS] Destructor cleanup test\n";
}

// Tests independent Winsock context ownership.
static void testIndependentContexts()
{
    WinsockContext firstContext;
    WinsockContext secondContext;
    WinsockContext inactiveContext;

    check(
        firstContext.initialize() == 0,
        "First context initialization failed"
    );

    check(
        secondContext.initialize() == 0,
        "Second context initialization failed"
    );

    check(
        firstContext.isInitialized(),
        "First context is inactive"
    );

    check(
        secondContext.isInitialized(),
        "Second context is inactive"
    );

    check(
        inactiveContext.cleanup() == 0,
        "Inactive context cleanup failed"
    );

    checkSocketAvailable();

    check(
        firstContext.cleanup() == 0,
        "First context cleanup failed"
    );

    check(
        !firstContext.isInitialized(),
        "First context is still initialized"
    );

    check(
        secondContext.isInitialized(),
        "Second context was incorrectly released"
    );

    // Winsock must remain available while the second context is active.
    checkSocketAvailable();

    check(
        firstContext.cleanup() == 0,
        "Repeated first context cleanup failed"
    );

    checkSocketAvailable();

    check(
        secondContext.cleanup() == 0,
        "Second context cleanup failed"
    );

    check(
        !secondContext.isInitialized(),
        "Second context is still initialized"
    );

    checkWinsockInactive();

    std::cout << "[PASS] Independent Winsock contexts test\n";
}

// Runs all Winsock context tests.
void runWinsockContextTests()
{
    // This test suite requires exclusive Winsock initialization ownership.
    checkWinsockInactive();

    testInitialState();
    testInitialization();
    testRepeatedInitialization();
    testRepeatedCleanup();
    testReinitialization();
    testDestructorCleanup();
    testIndependentContexts();

    checkWinsockInactive();

    std::cout << "All Winsock context tests passed.\n";
}