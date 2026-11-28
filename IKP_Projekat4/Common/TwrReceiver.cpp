#include "pch.h"

#include <winsock2.h>
#include <Windows.h>
#include <process.h>

#include <exception>

#include "TwrReceiver.h"
#include "PayloadCodec.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr uint32_t SELECT_TIMEOUT_MS = 50;

    constexpr int MAX_HANDOFFS_PER_POLL = 16;

    constexpr int MAX_MESSAGES_PER_POLL = 16;
}

// Initializes an inactive TWR receiver.
TwrReceiver::TwrReceiver(
    ConnectionHandoff& handoff
) noexcept
    : handoff_(handoff),
    workers_{},
    nextSessionId_(1),
    stopEvent_(nullptr),
    thread_(nullptr),
    workingStatistics_{},
    publishedStatistics_{},
    publishedWorkers_{}
{
    InitializeCriticalSection(&statisticsLock_);
}

// Stops the thread before destroying shared state.
TwrReceiver::~TwrReceiver() noexcept
{
    stop();

    if (thread_ != nullptr)
    {
        std::terminate();
    }

    DeleteCriticalSection(&statisticsLock_);
}

// Starts the actual TWR thread.
int TwrReceiver::start()
{
    if (thread_ != nullptr ||
        stopEvent_ != nullptr ||
        handoff_.isSealed())
    {
        return WSAEINVAL;
    }

    workingStatistics_ = TwrStatistics{};

    publishStatistics();

    stopEvent_ = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        nullptr
    );

    if (stopEvent_ == nullptr)
    {
        return static_cast<int>(GetLastError());
    }

    const uintptr_t threadHandle =
        _beginthreadex(
            nullptr,
            0,
            threadEntry,
            this,
            0,
            nullptr
        );

    if (threadHandle == 0)
    {
        CloseHandle(stopEvent_);

        stopEvent_ = nullptr;

        return WSAENOBUFS;
    }

    thread_ =
        reinterpret_cast<HANDLE>(threadHandle);

    return 0;
}

// Signals shutdown and waits for TWR completion.
int TwrReceiver::stop() noexcept
{
    if (thread_ != nullptr)
    {
        if (!SetEvent(stopEvent_))
        {
            return static_cast<int>(GetLastError());
        }

        const DWORD waitResult =
            WaitForSingleObject(
                thread_,
                INFINITE
            );

        if (waitResult != WAIT_OBJECT_0)
        {
            return waitResult == WAIT_FAILED
                ? static_cast<int>(GetLastError())
                : WSAEINVAL;
        }

        CloseHandle(thread_);

        thread_ = nullptr;
    }

    if (stopEvent_ != nullptr)
    {
        CloseHandle(stopEvent_);

        stopEvent_ = nullptr;
    }

    int result = getStatistics().lastError;

    // Once the handoff is sealed, no new connections
    // can be transferred into the queue.
    if (handoff_.isSealed())
    {
        ConnectionContext* connection = nullptr;

        while ((connection = handoff_.tryPop())
            != nullptr)
        {
            const int cleanupResult =
                connection->close();

            if (result == 0 &&
                cleanupResult != 0)
            {
                result = cleanupResult;
            }

            delete connection;
        }
    }

    return result;
}

// Returns a synchronized statistics snapshot.
TwrStatistics TwrReceiver::getStatistics()
noexcept
{
    EnterCriticalSection(&statisticsLock_);

    const TwrStatistics result =
        publishedStatistics_;

    LeaveCriticalSection(&statisticsLock_);

    return result;
}

// Returns a synchronized copy of worker information.
bool TwrReceiver::getWorker(
    uint64_t workerId,
    TwrWorkerInfo& information
) noexcept
{
    if (workerId == 0)
    {
        return false;
    }

    EnterCriticalSection(&statisticsLock_);

    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        if (publishedWorkers_[i].workerId ==
            workerId)
        {
            information =
                publishedWorkers_[i];

            LeaveCriticalSection(
                &statisticsLock_
            );

            return true;
        }
    }

    LeaveCriticalSection(&statisticsLock_);

    return false;
}

// Publishes a consistent diagnostic snapshot.
void TwrReceiver::publishStatistics() noexcept
{
    EnterCriticalSection(&statisticsLock_);

    publishedStatistics_ =
        workingStatistics_;

    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        publishedWorkers_[i] =
            workers_[i].information;
    }

    LeaveCriticalSection(&statisticsLock_);
}

// Checks the shutdown event.
bool TwrReceiver::shutdownRequested()
const noexcept
{
    return WaitForSingleObject(
        stopEvent_,
        0
    ) == WAIT_OBJECT_0;
}

// Windows thread entry point.
unsigned __stdcall TwrReceiver::threadEntry(
    void* parameter
)
{
    TwrReceiver* receiver =
        static_cast<TwrReceiver*>(parameter);

    return receiver->run();
}

// Executes the TWR event loop.
unsigned TwrReceiver::run()
{
    while (!shutdownRequested())
    {
        const int result = pollOnce();

        if (result != 0 &&
            workingStatistics_.lastError == 0)
        {
            workingStatistics_.lastError =
                result;
        }

        publishStatistics();

        if (workingStatistics_.lastError != 0)
        {
            break;
        }
    }

    // Release all sockets owned by TWR.
    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        closeWorker(i, false);
    }

    publishStatistics();

    return workingStatistics_.lastError == 0
        ? 0
        : 1;
}

// Processes one TWR event-loop iteration.
int TwrReceiver::pollOnce()
{
    // Take ownership of pending worker connections.
    adoptConnections();

    if (workingStatistics_.lastError != 0)
    {
        return workingStatistics_.lastError;
    }

    if (shutdownRequested())
    {
        return 0;
    }

    fd_set readSet;

    FD_ZERO(&readSet);

    bool hasBufferedWork = false;

    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        ConnectionContext* connection =
            workers_[i].connection;

        if (connection == nullptr)
        {
            continue;
        }

        FD_SET(
            connection->getSocket(),
            &readSet
        );

        if (connection->needsDrain)
        {
            hasBufferedWork = true;
        }
    }

    // Winsock select cannot wait on an empty socket set.
    if (readSet.fd_count == 0)
    {
        const DWORD result =
            WaitForSingleObject(
                stopEvent_,
                SELECT_TIMEOUT_MS
            );

        if (result == WAIT_FAILED)
        {
            return static_cast<int>(GetLastError());
        }

        return 0;
    }

    // Previously buffered messages must be processed
    // even if the network socket is not readable.
    const uint32_t timeoutMs =
        hasBufferedWork || !handoff_.isEmpty()
        ? 0
        : SELECT_TIMEOUT_MS;

    timeval timeout = {};

    timeout.tv_sec =
        static_cast<long>(timeoutMs / 1000);

    timeout.tv_usec =
        static_cast<long>(
            (timeoutMs % 1000) * 1000
            );

    const int selected = select(
        0,
        &readSet,
        nullptr,
        nullptr,
        &timeout
    );

    if (selected == SOCKET_ERROR)
    {
        return WSAGetLastError();
    }

    if (shutdownRequested())
    {
        return 0;
    }

    // Receive messages from active worker sockets.
    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        if (shutdownRequested())
        {
            return 0;
        }

        ConnectionContext* connection =
            workers_[i].connection;

        if (connection == nullptr)
        {
            continue;
        }

        const bool socketReady =
            selected > 0 &&
            FD_ISSET(
                connection->getSocket(),
                &readSet
            );

        if (socketReady ||
            connection->needsDrain)
        {
            processWorker(i);
        }

        if (workingStatistics_.lastError != 0)
        {
            return workingStatistics_.lastError;
        }
    }

    return 0;
}

// Takes ownership of connections transferred by TUR.
void TwrReceiver::adoptConnections()
{
    for (int i = 0;
        i < MAX_HANDOFFS_PER_POLL;
        ++i)
    {
        if (shutdownRequested())
        {
            return;
        }

        ConnectionContext* connection =
            handoff_.tryPop();

        if (connection == nullptr)
        {
            return;
        }

        // Validate the handed-off context.
        if (!connection->hasSocket() ||
            connection->role != ConnectionRole::Worker ||
            connection->workerId == 0 ||
            connection->sessionId != 0)
        {
            rejectConnection(connection);

            continue;
        }

        // An active worker identifier must be unique.
        if (workerExists(connection->workerId))
        {
            rejectConnection(connection);

            continue;
        }

        const std::size_t slot =
            findFreeSlot();

        if (slot == MAX_WORKERS ||
            nextSessionId_ == 0)
        {
            rejectConnection(connection);

            continue;
        }

        // Assign a new session identifier.
        connection->sessionId =
            nextSessionId_++;

        connection->needsDrain = true;

        WorkerSlot& worker = workers_[slot];

        worker.connection = connection;

        worker.information = TwrWorkerInfo{};

        worker.information.workerId =
            connection->workerId;

        worker.information.sessionId =
            connection->sessionId;

        // Registration time is the initial liveness baseline.
        worker.information.lastHeartbeatAt =
            GetTickCount64();

        ++workingStatistics_.activeWorkers;

        ++workingStatistics_.registeredWorkers;
    }
}

// Processes messages from one registered worker.
void TwrReceiver::processWorker(
    std::size_t index
)
{
    WorkerSlot& worker = workers_[index];

    ConnectionContext* connection =
        worker.connection;

    if (connection == nullptr)
    {
        return;
    }

    connection->needsDrain = false;

    for (int i = 0;
        i < MAX_MESSAGES_PER_POLL;
        ++i)
    {
        if (shutdownRequested())
        {
            return;
        }

        int socketError = 0;

        const ReceiveStatus status =
            connection->receiver.receive(
                connection->getSocket(),
                socketError
            );

        if (status == ReceiveStatus::WouldBlock)
        {
            return;
        }

        if (status == ReceiveStatus::ConnectionClosed ||
            status == ReceiveStatus::SocketError)
        {
            closeWorker(index, false);

            return;
        }

        if (status != ReceiveStatus::MessageReady)
        {
            closeWorker(index, true);

            return;
        }

        MessageHeader header = {};

        const uint8_t* payload = nullptr;

        if (!connection->receiver.getMessage(
            header,
            payload))
        {
            closeWorker(index, true);

            return;
        }

        // STEP 1: Process heartbeat messages.
        if (header.type ==
            MessageType::HEARTBEAT)
        {
            HeartbeatPayload heartbeat;

            if (!PayloadCodec::deserializeHeartbeat(
                payload,
                header.length,
                heartbeat))
            {
                closeWorker(index, true);

                return;
            }

            worker.information.lastHeartbeatAt =
                GetTickCount64();

            ++worker.information.observedHeartbeats;

            ++workingStatistics_.observedHeartbeats;
        }
        // STEP 2: Process result messages.
        else if (header.type ==
            MessageType::RESULT)
        {
            ResultPayload result;

            if (!PayloadCodec::deserializeResult(
                payload,
                header.length,
                result))
            {
                closeWorker(index, true);

                return;
            }

            // Diagnostic observation only.
            // Actual result validation is implemented later.
            worker.information.lastLbRequestId =
                result.lbRequestId;

            worker.information.lastAttemptId =
                result.attemptId;

            ++worker.information.observedResults;

            ++workingStatistics_.observedResults;
        }
        else
        {
            // Registered workers may only send
            // HEARTBEAT and RESULT messages.
            closeWorker(index, true);

            return;
        }

        if (!connection->receiver.consumeMessage())
        {
            closeWorker(index, true);

            return;
        }
    }

    // More complete messages may already be buffered.
    connection->needsDrain = true;
}

// Releases a registered worker connection.
void TwrReceiver::closeWorker(
    std::size_t index,
    bool rejected
) noexcept
{
    WorkerSlot& worker = workers_[index];

    if (worker.connection == nullptr)
    {
        return;
    }

    const int result =
        worker.connection->close();

    if (result != 0 &&
        workingStatistics_.lastError == 0)
    {
        workingStatistics_.lastError =
            result;
    }

    delete worker.connection;

    worker.connection = nullptr;

    worker.information = TwrWorkerInfo{};

    --workingStatistics_.activeWorkers;

    ++workingStatistics_.disconnectedWorkers;

    if (rejected)
    {
        ++workingStatistics_.rejectedWorkers;
    }
}

// Releases an unaccepted worker connection.
void TwrReceiver::rejectConnection(
    ConnectionContext* connection
) noexcept
{
    if (connection == nullptr)
    {
        return;
    }

    ++workingStatistics_.rejectedWorkers;

    const int result = connection->close();

    if (result != 0 &&
        workingStatistics_.lastError == 0)
    {
        workingStatistics_.lastError =
            result;
    }

    delete connection;
}

// Finds an unused worker slot.
std::size_t TwrReceiver::findFreeSlot()
const noexcept
{
    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        if (workers_[i].connection == nullptr)
        {
            return i;
        }
    }

    return MAX_WORKERS;
}

// Checks whether a worker identifier is already active.
bool TwrReceiver::workerExists(
    uint64_t workerId
) const noexcept
{
    for (std::size_t i = 0;
        i < MAX_WORKERS;
        ++i)
    {
        if (workers_[i].connection != nullptr &&
            workers_[i].information.workerId ==
            workerId)
        {
            return true;
        }
    }

    return false;
}