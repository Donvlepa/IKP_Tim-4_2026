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

// Initializes TWR in diagnostic mode.
TwrReceiver::TwrReceiver(
    ConnectionHandoff& handoff
) noexcept
    : handoff_(handoff),
    workerRegistry_(nullptr),
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

// Initializes TWR with a shared worker registry.
TwrReceiver::TwrReceiver(
    ConnectionHandoff& handoff,
    WorkerRegistry& workerRegistry
) noexcept
    : TwrReceiver(handoff)
{
    workerRegistry_ = &workerRegistry;
}

// Stops the receiver before releasing shared resources.
TwrReceiver::~TwrReceiver() noexcept
{
    stop();

    if (thread_ != nullptr)
    {
        std::terminate();
    }

    DeleteCriticalSection(&statisticsLock_);
}

// Starts the TWR thread.
int TwrReceiver::start()
{
    if (thread_ != nullptr ||
        stopEvent_ != nullptr ||
        handoff_.isSealed())
    {
        return WSAEINVAL;
    }

    // Production mode requires a valid worker registry.
    if (workerRegistry_ != nullptr &&
        !workerRegistry_->isValid())
    {
        const int error =
            workerRegistry_->getFatalError();

        return error != 0
            ? error
            : WSAEINVAL;
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

    // Release connections that were never adopted.
    // These connections have not been registered yet.
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

    if (result == 0 &&
        workerRegistry_ != nullptr)
    {
        result = workerRegistry_->getFatalError();
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

// Returns a synchronized diagnostic worker snapshot.
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

// Publishes consistent diagnostic information.
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

// Runs the TWR event loop.
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

    // Close only sockets owned by TWR.
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

// Processes one TWR polling iteration.
int TwrReceiver::pollOnce()
{
    adoptConnections();

    if (workingStatistics_.lastError != 0)
    {
        return workingStatistics_.lastError;
    }

    if (workerRegistry_ != nullptr)
    {
        const int registryError =
            workerRegistry_->getFatalError();

        if (registryError != 0)
        {
            return registryError;
        }
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

    // Winsock select requires at least one socket.
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

    // Process existing worker connections.
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

// Registers a worker and assigns a session identifier.
int TwrReceiver::registerWorkerConnection(
    ConnectionContext& connection,
    uint64_t heartbeatTimeMs
)
{
    // Production registration is handled by Worker Registry.
    if (workerRegistry_ != nullptr)
    {
        uint64_t sessionId = 0;

        const int result =
            workerRegistry_->registerWorker(
                connection.workerId,
                connection.getSocket(),
                heartbeatTimeMs,
                sessionId
            );

        // A nonzero session identifies committed state,
        // even if semaphore signaling failed afterward.
        connection.sessionId = sessionId;

        return result;
    }

    // Preserve diagnostic mode for previous tests.
    if (workerExists(connection.workerId))
    {
        return WSAEALREADY;
    }

    if (nextSessionId_ == 0)
    {
        return WSAENOBUFS;
    }

    connection.sessionId =
        nextSessionId_++;

    return 0;
}

// Takes ownership of worker connections transferred by TUR.
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

        if (!connection->hasSocket() ||
            connection->role != ConnectionRole::Worker ||
            connection->workerId == 0 ||
            connection->sessionId != 0)
        {
            // Invalid handoff contexts are not registered.
            connection->sessionId = 0;

            rejectConnection(connection);

            continue;
        }

        // Check local socket capacity before registration.
        const std::size_t slot =
            findFreeSlot();

        if (slot == MAX_WORKERS)
        {
            rejectConnection(connection);

            continue;
        }

        const uint64_t heartbeatTimeMs =
            GetTickCount64();

        const int result =
            registerWorkerConnection(
                *connection,
                heartbeatTimeMs
            );

        if (result != 0)
        {
            if (workerRegistry_ != nullptr)
            {
                const int fatalError =
                    workerRegistry_->getFatalError();

                if (fatalError != 0)
                {
                    workingStatistics_.lastError =
                        fatalError;
                }
            }

            rejectConnection(connection);

            if (workingStatistics_.lastError != 0)
            {
                return;
            }

            continue;
        }

        // Store the owned network connection locally.
        connection->needsDrain = true;

        WorkerSlot& worker = workers_[slot];

        worker.connection = connection;

        worker.information = TwrWorkerInfo{};

        worker.information.workerId =
            connection->workerId;

        worker.information.sessionId =
            connection->sessionId;

        worker.information.lastHeartbeatAt =
            heartbeatTimeMs;

        ++workingStatistics_.activeWorkers;

        ++workingStatistics_.registeredWorkers;
    }
}

// Updates the heartbeat of the matching worker session.
bool TwrReceiver::handleHeartbeat(
    WorkerSlot& worker
) noexcept
{
    ConnectionContext* connection =
        worker.connection;

    if (connection == nullptr)
    {
        return false;
    }

    const uint64_t heartbeatTimeMs =
        GetTickCount64();

    if (workerRegistry_ != nullptr)
    {
        if (!workerRegistry_->updateHeartbeat(
            connection->workerId,
            connection->sessionId,
            heartbeatTimeMs))
        {
            // The session is no longer active.
            return false;
        }
    }

    worker.information.lastHeartbeatAt =
        heartbeatTimeMs;

    ++worker.information.observedHeartbeats;

    ++workingStatistics_.observedHeartbeats;

    return true;
}

// Receives messages from one worker connection.
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
        if (header.type == MessageType::HEARTBEAT)
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

            if (!handleHeartbeat(worker))
            {
                closeWorker(index, false);

                return;
            }
        }
        // STEP 2: Observe result messages.
        else if (header.type == MessageType::RESULT)
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

            // Result processing and assignment validation
            // will be implemented in a later phase.
            worker.information.lastLbRequestId =
                result.lbRequestId;

            worker.information.lastAttemptId =
                result.attemptId;

            ++worker.information.observedResults;

            ++workingStatistics_.observedResults;
        }
        else
        {
            closeWorker(index, true);

            return;
        }

        if (!connection->receiver.consumeMessage())
        {
            closeWorker(index, true);

            return;
        }
    }

    // Drain any remaining buffered messages later.
    connection->needsDrain = true;
}

// Releases an active worker connection.
void TwrReceiver::closeWorker(
    std::size_t index,
    bool rejected
) noexcept
{
    WorkerSlot& worker = workers_[index];

    ConnectionContext* connection =
        worker.connection;

    if (connection == nullptr)
    {
        return;
    }

    // Mark the session DEAD before closing its socket.
    if (workerRegistry_ != nullptr &&
        connection->sessionId != 0)
    {
        WorkerEntry previousState;

        workerRegistry_->markDead(
            connection->workerId,
            connection->sessionId,
            previousState
        );

        // Recovery of an assigned request will be
        // implemented together with dispatch and failure handling.
    }

    const int result =
        connection->close();

    if (result != 0 &&
        workingStatistics_.lastError == 0)
    {
        workingStatistics_.lastError =
            result;
    }

    delete connection;

    worker.connection = nullptr;

    worker.information = TwrWorkerInfo{};

    --workingStatistics_.activeWorkers;

    ++workingStatistics_.disconnectedWorkers;

    if (rejected)
    {
        ++workingStatistics_.rejectedWorkers;
    }
}

// Releases a rejected worker connection.
void TwrReceiver::rejectConnection(
    ConnectionContext* connection
) noexcept
{
    if (connection == nullptr)
    {
        return;
    }

    ++workingStatistics_.rejectedWorkers;

    // Registration may have committed before a fatal
    // semaphore error occurred.
    if (workerRegistry_ != nullptr &&
        connection->sessionId != 0)
    {
        WorkerEntry previousState;

        workerRegistry_->markDead(
            connection->workerId,
            connection->sessionId,
            previousState
        );
    }

    const int result =
        connection->close();

    if (result != 0 &&
        workingStatistics_.lastError == 0)
    {
        workingStatistics_.lastError =
            result;
    }

    delete connection;
}

// Finds an unused local socket slot.
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

// Checks for a duplicate worker in diagnostic mode.
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