#include <winsock2.h>
#include <Windows.h>

#include <iostream>

#include "WinsockContext.h"
#include "ConnectionHandoff.h"
#include "TurReceiver.h"
#include "TwrReceiver.h"
#include "RequestRegistry.h"
#include "WorkerRegistry.h"
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
                &shutdownRequested, 1
            );

            return TRUE;
        }

        default:
            return FALSE;
        }
    }

    // Checks whether shutdown was requested.
    bool isShutdownRequested()
    {
        return InterlockedCompareExchange(
            &shutdownRequested,
            0,
            0
        ) != 0;
    }

    // Detects changes in TUR statistics.
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

    // Detects changes in TWR statistics.
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

    // Prints receiver and registry statistics.
    void printStatistics(
        const TurStatistics& tur,
        const TwrStatistics& twr,
        RequestRegistry& requests,
        WorkerRegistry& workers
    )
    {
        std::cout
            << "\n================================\n"
            << "       LB SYSTEM STATISTICS\n"
            << "================================\n"

            << "\n[TUR]\n"

            << "Pending connections: "
            << tur.pendingConnections << "\n"

            << "Active users: "
            << tur.activeUsers << "\n"

            << "Handed-off workers: "
            << tur.handedOffWorkers << "\n"

            << "Accepted requests: "
            << tur.observedRequests << "\n"

            << "Rejected connections: "
            << tur.rejectedConnections << "\n"

            << "\n[TWR]\n"

            << "Active worker connections: "
            << twr.activeWorkers << "\n"

            << "Registered worker sessions: "
            << twr.registeredWorkers << "\n"

            << "Disconnected workers: "
            << twr.disconnectedWorkers << "\n"

            << "Rejected workers: "
            << twr.rejectedWorkers << "\n"

            << "Observed heartbeats: "
            << twr.observedHeartbeats << "\n"

            << "Observed results: "
            << twr.observedResults << "\n"

            << "\n[REQUEST REGISTRY]\n"

            << "Stored requests: "
            << requests.size() << "\n"

            << "QH (HIGH): "
            << requests.getQueueSize(
                Priority::HIGH) << "\n"

            << "QM (MEDIUM): "
            << requests.getQueueSize(
                Priority::MEDIUM) << "\n"

            << "QL (LOW): "
            << requests.getQueueSize(
                Priority::LOW) << "\n"

            << "\n[WORKER REGISTRY]\n"

            << "Stored worker records: "
            << workers.size() << "\n"

            << "Active workers: "
            << workers.getActiveWorkerCount() << "\n"

            << "Free workers: "
            << workers.getFreeWorkerCount() << "\n"

            << "Free Worker Queue items: "
            << workers.getFreeQueueSize() << "\n"

            << "================================\n";
    }

    // Runs the load balancer subsystem.
    int runServer()
    {
        // STEP 1: Initialize shared registries.
        RequestRegistry requestRegistry;

        if (!requestRegistry.isValid())
        {
            std::cerr
                << "[ERROR] Request Registry initialization failed."
                << " Error: "
                << requestRegistry.getFatalError()
                << "\n";

            return 1;
        }

        WorkerRegistry workerRegistry;

        if (!workerRegistry.isValid())
        {
            std::cerr
                << "[ERROR] Worker Registry initialization failed."
                << " Error: "
                << workerRegistry.getFatalError()
                << "\n";

            return 1;
        }

        std::cout
            << "[LB] Request Registry initialized.\n"
            << "[LB] Priority queues initialized.\n"
            << "[LB] semRequests initialized.\n"

            << "[LB] Worker Registry initialized.\n"
            << "[LB] Free Worker Queue initialized.\n"
            << "[LB] semFreeWorkers initialized.\n";

        // Shared resources must outlive both receivers.
        ConnectionHandoff handoff;

        TurReceiver tur(
            handoff,
            requestRegistry
        );

        TwrReceiver twr(
            handoff,
            workerRegistry
        );

        // STEP 2: Start the worker receiver.
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

        // STEP 3: Start the user receiver and listener.
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

            << "[LB] Maximum stored requests: "
            << RequestRegistry::MAX_REQUESTS << "\n"

            << "[LB] Server is running.\n"

            << "[LB] Press Ctrl+C to stop the server.\n";

        int exitCode = 0;

        TurStatistics previousTur =
            tur.getStatistics();

        TwrStatistics previousTwr =
            twr.getStatistics();

        printStatistics(
            previousTur,
            previousTwr,
            requestRegistry,
            workerRegistry
        );

        // STEP 4: Monitor receiver and registry health.
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
                    currentTwr,
                    requestRegistry,
                    workerRegistry
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

            const int requestError =
                requestRegistry.getFatalError();

            if (requestError != 0)
            {
                std::cerr
                    << "[ERROR] Request Registry failure."
                    << " Error: "
                    << requestError << "\n";

                exitCode = 1;

                break;
            }

            const int workerError =
                workerRegistry.getFatalError();

            if (workerError != 0)
            {
                std::cerr
                    << "[ERROR] Worker Registry failure."
                    << " Error: "
                    << workerError << "\n";

                exitCode = 1;

                break;
            }

            Sleep(100);
        }

        std::cout
            << "\n[LB] Shutdown requested.\n";

        // STEP 5: Stop accepting new connections.
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

        // STEP 6: Prevent further worker handoffs.
        handoff.seal();

        std::cout
            << "[LB] Connection handoff sealed.\n";

        // STEP 7: Stop worker reception.
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

        // STEP 8: Print final statistics.
        printStatistics(
            tur.getStatistics(),
            twr.getStatistics(),
            requestRegistry,
            workerRegistry
        );

        if (!handoff.isEmpty())
        {
            std::cerr
                << "[ERROR] Connection handoff is not empty.\n";

            exitCode = 1;
        }
        else
        {
            std::cout
                << "[LB] Connection handoff is empty.\n";
        }

        if (workerRegistry.getActiveWorkerCount() != 0 ||
            workerRegistry.getFreeWorkerCount() != 0)
        {
            std::cerr
                << "[ERROR] Worker Registry retained active workers.\n";

            exitCode = 1;
        }

        std::cout
            << "[LB] Remaining stored requests: "
            << requestRegistry.size() << "\n"

            << "[LB] Remaining worker records: "
            << workerRegistry.size() << "\n"

            << "[LB] Registry and queue resources will be released.\n";

        // Receivers are destroyed before shared registries.
        // All resources are released before WSACleanup.
        return exitCode;
    }
}

int main()
{
    std::cout
        << "================================\n"
        << "          LOAD BALANCER          \n"
        << "================================\n\n";

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