#include "pch.h"
#include <winsock2.h>
#include <Windows.h>

#include <iostream>

#include "WinsockContext.h"
#include "RegistrationServer.h"
#include "Constants.h"

namespace
{
    volatile LONG shutdownRequested = 0;

    // Handles console shutdown signals.
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
        << "================================\n"
        << "          LOAD BALANCER          \n"
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

    // STEP 2: Start the registration server.
    RegistrationServer server;

    result = server.start(
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
        << "[LB] Listening on port "
        << server.getPort() << ".\n"
        << "[LB] Server is running.\n"
        << "[LB] Maximum connections: "
        << RegistrationServer::MAX_CONNECTIONS << "\n"
        << "[LB] Press Ctrl+C to stop the server.\n\n";

    int exitCode = 0;

    // STEP 4: Process TCP connections and messages.
    while (!isShutdownRequested())
    {
        result = server.poll(100);

        if (result != 0)
        {
            std::cerr
                << "[ERROR] Server polling failed."
                << " Error: " << result << "\n";

            exitCode = 1;

            break;
        }
    }

    // STEP 5: Print the final server statistics.
    std::cout
        << "\n[LB] Shutting down server.\n"
        << "[LB] Active connections: "
        << server.getConnectionCount() << "\n"
        << "[LB] Registered users: "
        << server.getUserCount() << "\n"
        << "[LB] Registered workers: "
        << server.getWorkerCount() << "\n"
        << "[LB] Observed requests: "
        << server.getObservedRequestCount() << "\n";

    // STEP 6: Release network resources.
    result = server.stop();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Server cleanup failed."
            << " Error: " << result << "\n";

        exitCode = 1;
    }
    else
    {
        std::cout
            << "[LB] All server sockets closed.\n";
    }

    // STEP 7: Release Winsock resources.
    result = winsock.cleanup();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Winsock cleanup failed."
            << " Error: " << result << "\n";

        exitCode = 1;
    }
    else
    {
        std::cout
            << "[LB] Winsock resources released.\n";
    }

    SetConsoleCtrlHandler(
        consoleControlHandler,
        FALSE
    );

    if (exitCode == 0)
    {
        std::cout
            << "[LB] Server stopped successfully.\n";
    }

    return exitCode;
}