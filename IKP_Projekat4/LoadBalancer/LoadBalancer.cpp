#include "pch.h"
#include <winsock2.h>
#include <Windows.h>

#include <iostream>

#include "WinsockContext.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "TwrReceiver.h"
#include "Constants.h"

namespace
{
    volatile LONG shutdownRequested = 0;

    // Handles console shutdown signals.
    BOOL WINAPI consoleControlHandler(
        DWORD controlType
    )
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
            return FALSE;
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

    // Detects changes in the TUR diagnostic statistics.
    bool turStatisticsChanged(
        const TurStatistics& previous,
        const TurStatistics& current
    )
    {
        return
            previous.pendingConnections !=
            current.pendingConnections ||

            previous.activeUsers !=
            current.activeUsers ||

            previous.handedOffWorkers !=
            current.handedOffWorkers ||

            previous.observedRequests !=
            current.observedRequests ||

            previous.rejectedConnections !=
            current.rejectedConnections ||

            previous.lastError !=
            current.lastError;
    }

    // Detects changes in the TWR diagnostic statistics.
    bool twrStatisticsChanged(
        const TwrStatistics& previous,
        const TwrStatistics& current
    )
    {
        return
            previous.activeWorkers !=
            current.activeWorkers ||

            previous.registeredWorkers !=
            current.registeredWorkers ||

            previous.rejectedWorkers !=
            current.rejectedWorkers ||

            previous.disconnectedWorkers !=
            current.disconnectedWorkers ||

            previous.observedHeartbeats !=
            current.observedHeartbeats ||

            previous.observedResults !=
            current.observedResults ||

            previous.lastError !=
            current.lastError;
    }

    // Prints the current receiver statistics.
    void printStatistics(
        const TurStatistics& tur,
        const TwrStatistics& twr
    )
    {
        std::cout
            << "\n--------------------------------\n"
            << "[LB] Receiver statistics\n"
            << "--------------------------------\n"

            << "[TUR] Pending connections: "
            << tur.pendingConnections << "\n"

            << "[TUR] Active users: "
            << tur.activeUsers << "\n"

            << "[TUR] Handed-off workers: "
            << tur.handedOffWorkers << "\n"

            << "[TUR] Observed requests: "
            << tur.observedRequests << "\n"

            << "[TUR] Rejected connections: "
            << tur.rejectedConnections << "\n"

            << "[TWR] Active workers: "
            << twr.activeWorkers << "\n"

            << "[TWR] Registered worker sessions: "
            << twr.registeredWorkers << "\n"

            << "[TWR] Disconnected workers: "
            << twr.disconnectedWorkers << "\n"

            << "[TWR] Rejected workers: "
            << twr.rejectedWorkers << "\n"

            << "[TWR] Observed heartbeats: "
            << twr.observedHeartbeats << "\n"

            << "[TWR] Observed results: "
            << twr.observedResults << "\n"

            << "--------------------------------\n";
    }

    // Runs the load balancer receiver subsystem.
    int runServer()
    {
        ConnectionHandoff handoff;

        TurReceiver tur(handoff);
        TwrReceiver twr(handoff);

        // STEP 1: Start the worker receiver first.
        int result = twr.start();

        if (result != 0)
        {
            std::cerr
                << "[ERROR] TWR startup failed."
                << " Error: " << result << "\n";

            return 1;
        }

        std::cout
            << "[LB] TWR thread started successfully.\n";

        // STEP 2: Start the user receiver and listener.
        result = tur.start(
            "0.0.0.0",
            Constants::SERVER_PORT
        );

        if (result != 0)
        {
            std::cerr
                << "[ERROR] TUR startup failed."
                << " Error: " << result << "\n";

            handoff.seal();

            const int cleanupResult =
                twr.stop();

            if (cleanupResult != 0)
            {
                std::cerr
                    << "[ERROR] TWR cleanup failed."
                    << " Error: "
                    << cleanupResult << "\n";
            }

            return 1;
        }

        std::cout
            << "[LB] TUR thread started successfully.\n"

            << "[LB] Listening on port "
            << tur.getPort() << ".\n"

            << "[LB] Maximum TUR connections: "
            << TurReceiver::MAX_CONNECTIONS << "\n"

            << "[LB] Maximum TWR workers: "
            << TwrReceiver::MAX_WORKERS << "\n"

            << "[LB] Server is running.\n"
            << "[LB] Press Ctrl+C to stop the server.\n";

        int exitCode = 0;

        TurStatistics previousTur =
            tur.getStatistics();

        TwrStatistics previousTwr =
            twr.getStatistics();

        printStatistics(
            previousTur,
            previousTwr
        );

        // STEP 3: Monitor receiver health and statistics.
        while (!isShutdownRequested())
        {
            const TurStatistics currentTur =
                tur.getStatistics();

            const TwrStatistics currentTwr =
                twr.getStatistics();

            if (turStatisticsChanged(
                previousTur, currentTur) ||
                twrStatisticsChanged(
                    previousTwr, currentTwr))
            {
                printStatistics(
                    currentTur,
                    currentTwr
                );

                previousTur = currentTur;
                previousTwr = currentTwr;
            }

            if (currentTur.lastError != 0)
            {
                std::cerr
                    << "[ERROR] TUR internal failure."
                    << " Error: "
                    << currentTur.lastError << "\n";

                exitCode = 1;

                break;
            }

            if (currentTwr.lastError != 0)
            {
                std::cerr
                    << "[ERROR] TWR internal failure."
                    << " Error: "
                    << currentTwr.lastError << "\n";

                exitCode = 1;

                break;
            }

            // Only diagnostic monitoring happens here.
            // Network reception runs in TUR and TWR.
            Sleep(100);
        }

        std::cout
            << "\n[LB] Shutdown requested.\n";

        // STEP 4: Stop accepting and receiving user requests.
        std::cout
            << "[LB] Stopping TUR thread...\n";

        result = tur.stop();

        if (result != 0)
        {
            std::cerr
                << "[ERROR] TUR shutdown failed."
                << " Error: " << result << "\n";

            exitCode = 1;
        }
        else
        {
            std::cout
                << "[LB] TUR thread stopped.\n";
        }

        // STEP 5: Prevent further connection transfers.
        handoff.seal();

        std::cout
            << "[LB] Connection handoff sealed.\n";

        // STEP 6: Stop worker reception.
        std::cout
            << "[LB] Stopping TWR thread...\n";

        result = twr.stop();

        if (result != 0)
        {
            std::cerr
                << "[ERROR] TWR shutdown failed."
                << " Error: " << result << "\n";

            exitCode = 1;
        }
        else
        {
            std::cout
                << "[LB] TWR thread stopped.\n";
        }

        // STEP 7: Print final diagnostic statistics.
        printStatistics(
            tur.getStatistics(),
            twr.getStatistics()
        );

        if (handoff.isEmpty())
        {
            std::cout
                << "[LB] Connection handoff is empty.\n";
        }
        else
        {
            std::cerr
                << "[ERROR] Connection handoff is not empty.\n";

            exitCode = 1;
        }

        // All local objects are destroyed before Winsock cleanup.
        return exitCode;
    }
}

int main()
{
    std::cout
        << "================================\n"
        << "          LOAD BALANCER          \n"
        << "================================\n\n";

    // Initialize Winsock before creating receiver objects.
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

    // Register the console shutdown handler.
    if (!SetConsoleCtrlHandler(
        consoleControlHandler,
        TRUE))
    {
        std::cerr
            << "[ERROR] Console handler registration failed."
            << " Error: " << GetLastError() << "\n";

        return 1;
    }

    int exitCode = runServer();

    // Receiver objects have already been destroyed.
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