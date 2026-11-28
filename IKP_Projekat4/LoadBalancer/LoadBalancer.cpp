#include "pch.h"
#include <winsock2.h>
#include <Windows.h>

#include <iostream>

#include "WinsockContext.h"
#include "TcpListener.h"
#include "Constants.h"

namespace
{
    // Indicates whether server shutdown has been requested.
    volatile LONG shutdownRequested = 0;

    // Handles Ctrl+C and Ctrl+Break console signals.
    BOOL WINAPI consoleControlHandler(DWORD controlType)
    {
        switch (controlType)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        {
            InterlockedExchange(
                &shutdownRequested,
                1
            );

            return TRUE;
        }

        default:
        {
            return FALSE;
        }
        }
    }

    // Checks whether shutdown has been requested.
    bool isShutdownRequested()
    {
        return InterlockedCompareExchange(
            &shutdownRequested,
            0,
            0
        ) != 0;
    }
}

int main()
{
    std::cout
        << "================================\n";

    std::cout
        << "          LOAD BALANCER          \n";

    std::cout
        << "================================\n\n";

    // STEP 1: Initialize Winsock.
    WinsockContext winsock;

    int result = winsock.initialize();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Winsock initialization failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[LB] Winsock initialized successfully.\n";

    // STEP 2: Create the TCP listener.
    TcpListener listener;

    result = listener.start(
        "0.0.0.0",
        Constants::SERVER_PORT
    );

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Server startup failed."
            << " Error: " << result << "\n";

        return 1;
    }

    // STEP 3: Register the console control handler.
    if (!SetConsoleCtrlHandler(
        consoleControlHandler,
        TRUE))
    {
        std::cerr
            << "[ERROR] Console handler registration failed."
            << " Error: " << GetLastError() << "\n";

        return 1;
    }

    std::cout
        << "[LB] Server socket created successfully.\n";

    std::cout
        << "[LB] Listening on port "
        << listener.getPort()
        << ".\n";

    std::cout
        << "[LB] Server is running.\n";

    std::cout
        << "[LB] Press Ctrl+C to stop the server.\n\n";

    // STEP 4: Keep the server running.
    // Actual connection acceptance will be implemented later.
    while (!isShutdownRequested())
    {
        Sleep(100);
    }

    // STEP 5: Stop the server.
    std::cout
        << "\n[LB] Shutdown requested.\n";

    std::cout
        << "[LB] Closing listening socket...\n";

    result = listener.stop();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Listener cleanup failed."
            << " Error: " << result << "\n";

        SetConsoleCtrlHandler(
            consoleControlHandler,
            FALSE
        );

        return 1;
    }

    std::cout
        << "[LB] Listening socket closed.\n";

    // STEP 6: Release Winsock resources.
    result = winsock.cleanup();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Winsock cleanup failed."
            << " Error: " << result << "\n";

        SetConsoleCtrlHandler(
            consoleControlHandler,
            FALSE
        );

        return 1;
    }

    std::cout
        << "[LB] Winsock resources released.\n";

    SetConsoleCtrlHandler(
        consoleControlHandler,
        FALSE
    );

    std::cout
        << "[LB] Server stopped successfully.\n";

    return 0;
}