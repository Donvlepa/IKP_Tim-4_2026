#include <iostream>
#include <cassert>
#include "WorkerEntry.h"
#include "TestSuites.h"

// Tests default worker initialization.
static void testDefaultWorkerEntry()
{
    WorkerEntry worker;

    assert(worker.workerId == 0);
    assert(worker.sessionId == 0);
    assert(worker.workerSocket == INVALID_SOCKET);
    assert(worker.status == WorkerStatus::DEAD);
    assert(worker.lastHeartbeatMs == 0);
    assert(worker.currentRequestId == 0);

    std::cout << "Default worker entry test passed.\n";
}

// Tests initialization of a registered worker.
static void testWorkerInitialization()
{
    WorkerEntry worker;

    worker.workerId = 1;
    worker.sessionId = 101;
    worker.status = WorkerStatus::FREE;
    worker.lastHeartbeatMs = 10000;

    assert(worker.workerId == 1);
    assert(worker.sessionId == 101);
    assert(worker.status == WorkerStatus::FREE);
    assert(worker.lastHeartbeatMs == 10000);
    assert(worker.currentRequestId == 0);

    std::cout << "Worker initialization test passed.\n";
}

// Tests storage of worker assignment information.
static void testWorkerAssignment()
{
    WorkerEntry worker;

    worker.workerId = 2;
    worker.status = WorkerStatus::FREE;

    worker.currentRequestId = 205;
    worker.status = WorkerStatus::BUSY;

    assert(worker.currentRequestId == 205);
    assert(worker.status == WorkerStatus::BUSY);

    worker.currentRequestId = 0;
    worker.status = WorkerStatus::FREE;

    assert(worker.currentRequestId == 0);
    assert(worker.status == WorkerStatus::FREE);

    std::cout << "Worker assignment test passed.\n";
}

// Tests storage of heartbeat timestamps.
static void testWorkerHeartbeat()
{
    WorkerEntry worker;

    worker.lastHeartbeatMs = 10000;

    assert(worker.lastHeartbeatMs == 10000);

    worker.lastHeartbeatMs = 11000;

    assert(worker.lastHeartbeatMs == 11000);

    std::cout << "Worker heartbeat test passed.\n";
}

// Tests storage of a new worker session.
static void testWorkerSession()
{
    WorkerEntry worker;

    worker.workerId = 1;
    worker.sessionId = 101;
    worker.status = WorkerStatus::DEAD;

    worker.sessionId = 102;
    worker.status = WorkerStatus::FREE;

    assert(worker.workerId == 1);
    assert(worker.sessionId == 102);
    assert(worker.status == WorkerStatus::FREE);

    std::cout << "Worker session test passed.\n";
}

// Runs all worker entry tests.
void runWorkerEntryTests()
{
    testDefaultWorkerEntry();
    testWorkerInitialization();
    testWorkerAssignment();
    testWorkerHeartbeat();
    testWorkerSession();
}