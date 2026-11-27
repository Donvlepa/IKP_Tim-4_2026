#include <iostream>
#include <cstdlib>
#include <cstdint>
#include "Queue.h"
#include "TestSuites.h"

// Checks a test condition.
static void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";
        std::abort();
    }
}

// Tests a newly initialized queue.
static void testEmptyQueue()
{
    Queue<uint64_t> queue;

    check(queue.isEmpty(), "Queue must be empty");
    check(queue.size() == 0, "Initial size must be zero");

    uint64_t value = 999;

    check(!queue.dequeue(value), "Empty dequeue must fail");
    check(!queue.peek(value), "Empty peek must fail");
    check(value == 999, "Output value must remain unchanged");

    std::cout << "[PASS] Empty queue test\n";
}

// Tests insertion at the end of the queue.
static void testEnqueue()
{
    Queue<uint64_t> queue;

    check(queue.enqueue(10), "Enqueue 10 failed");
    check(queue.enqueue(20), "Enqueue 20 failed");
    check(queue.enqueue(30), "Enqueue 30 failed");

    check(!queue.isEmpty(), "Queue must not be empty");
    check(queue.size() == 3, "Queue size must be three");

    std::cout << "[PASS] Enqueue test\n";
}

// Tests FIFO ordering.
static void testFIFO()
{
    Queue<uint64_t> queue;

    check(queue.enqueue(10), "Enqueue failed");
    check(queue.enqueue(20), "Enqueue failed");
    check(queue.enqueue(30), "Enqueue failed");

    uint64_t value = 0;

    check(queue.dequeue(value), "First dequeue failed");
    check(value == 10, "Expected value 10");

    check(queue.dequeue(value), "Second dequeue failed");
    check(value == 20, "Expected value 20");

    check(queue.dequeue(value), "Third dequeue failed");
    check(value == 30, "Expected value 30");

    check(queue.isEmpty(), "Queue must be empty");
    check(queue.size() == 0, "Queue size must be zero");

    std::cout << "[PASS] FIFO ordering test\n";
}

// Tests insertion at the front of the queue.
static void testEnqueueFront()
{
    Queue<uint64_t> queue;

    check(queue.enqueue(20), "Enqueue failed");
    check(queue.enqueue(30), "Enqueue failed");

    check(queue.enqueueFront(10), "Enqueue front failed");
    check(queue.size() == 3, "Invalid queue size");

    uint64_t value = 0;

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 10, "Expected value 10");

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 20, "Expected value 20");

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 30, "Expected value 30");

    check(queue.isEmpty(), "Queue must be empty");

    std::cout << "[PASS] Enqueue front test\n";
}

// Tests insertion at the front of an empty queue.
static void testEnqueueFrontEmpty()
{
    Queue<uint64_t> queue;

    check(queue.enqueueFront(100), "Enqueue front failed");
    check(queue.enqueue(200), "Enqueue failed");

    uint64_t value = 0;

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 100, "Expected value 100");

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 200, "Expected value 200");

    check(queue.isEmpty(), "Queue must be empty");

    std::cout << "[PASS] Enqueue front on empty queue test\n";
}

// Tests reading without removing an element.
static void testPeek()
{
    Queue<uint64_t> queue;

    check(queue.enqueue(50), "Enqueue failed");
    check(queue.enqueue(60), "Enqueue failed");

    uint64_t value = 0;

    check(queue.peek(value), "Peek failed");
    check(value == 50, "Invalid first value");

    check(queue.size() == 2, "Peek changed queue size");

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 50, "Peek removed an element");

    std::cout << "[PASS] Peek test\n";
}

// Tests reuse after removing the last element.
static void testQueueReuse()
{
    Queue<uint64_t> queue;

    check(queue.enqueue(1), "Enqueue failed");

    uint64_t value = 0;

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 1, "Expected value 1");

    check(queue.isEmpty(), "Queue must be empty");

    check(queue.enqueue(2), "Queue reuse failed");
    check(queue.enqueueFront(3), "Enqueue front failed");

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 3, "Expected value 3");

    check(queue.dequeue(value), "Dequeue failed");
    check(value == 2, "Expected value 2");

    check(queue.isEmpty(), "Queue must be empty");
    check(queue.size() == 0, "Invalid queue size");

    std::cout << "[PASS] Queue reuse test\n";
}

// Tests clearing all stored elements.
static void testClear()
{
    Queue<uint64_t> queue;

    for (uint64_t i = 1; i <= 100; i++)
    {
        check(queue.enqueue(i), "Enqueue failed");
    }

    queue.clear();

    check(queue.isEmpty(), "Clear must empty the queue");
    check(queue.size() == 0, "Clear must reset size");

    queue.clear();

    check(queue.isEmpty(), "Repeated clear failed");

    check(queue.enqueue(500), "Enqueue after clear failed");

    uint64_t value = 0;

    check(queue.dequeue(value), "Dequeue after clear failed");
    check(value == 500, "Expected value 500");

    std::cout << "[PASS] Clear test\n";
}

// Tests a large number of elements.
static void testLargeQueue()
{
    Queue<uint64_t> queue;

    const uint64_t numberOfElements = 10000;

    for (uint64_t i = 1; i <= numberOfElements; i++)
    {
        check(queue.enqueue(i), "Large queue enqueue failed");
    }

    check(queue.size() == numberOfElements,
        "Invalid large queue size");

    for (uint64_t i = 1; i <= numberOfElements; i++)
    {
        uint64_t value = 0;

        check(queue.dequeue(value),
            "Large queue dequeue failed");

        check(value == i, "Large queue FIFO violation");
    }

    check(queue.isEmpty(), "Large queue must be empty");
    check(queue.size() == 0, "Final size must be zero");

    std::cout << "[PASS] Large queue test (10000 elements)\n";
}

// Tests independent queue instances.
static void testIndependentQueues()
{
    Queue<uint64_t> highQueue;
    Queue<uint64_t> mediumQueue;
    Queue<uint64_t> lowQueue;

    check(highQueue.enqueue(100), "High enqueue failed");
    check(mediumQueue.enqueue(200), "Medium enqueue failed");
    check(lowQueue.enqueue(300), "Low enqueue failed");

    uint64_t value = 0;

    check(highQueue.dequeue(value), "High dequeue failed");
    check(value == 100, "Invalid high queue value");

    check(mediumQueue.size() == 1, "Medium queue changed");
    check(lowQueue.size() == 1, "Low queue changed");

    check(mediumQueue.dequeue(value), "Medium dequeue failed");
    check(value == 200, "Invalid medium queue value");

    check(lowQueue.dequeue(value), "Low dequeue failed");
    check(value == 300, "Invalid low queue value");

    std::cout << "[PASS] Independent queues test\n";
}

// Tests storing custom structures.
static void testGenericQueue()
{
    struct WorkerItem
    {
        uint64_t workerId;
        uint64_t sessionId;
    };

    Queue<WorkerItem> queue;

    WorkerItem first = { 1, 101 };
    WorkerItem second = { 2, 102 };

    check(queue.enqueue(first), "First worker enqueue failed");
    check(queue.enqueue(second), "Second worker enqueue failed");

    // Verifies that queue nodes store independent values.
    first.workerId = 999;

    WorkerItem result = {};

    check(queue.dequeue(result), "Worker dequeue failed");
    check(result.workerId == 1, "Invalid worker ID");
    check(result.sessionId == 101, "Invalid session ID");

    check(queue.dequeue(result), "Worker dequeue failed");
    check(result.workerId == 2, "Invalid worker ID");
    check(result.sessionId == 102, "Invalid session ID");

    check(queue.isEmpty(), "Worker queue must be empty");

    std::cout << "[PASS] Generic queue test\n";
}

// Runs all linked queue tests.
void runQueueTests()
{
    testEmptyQueue();
    testEnqueue();
    testFIFO();
    testEnqueueFront();
    testEnqueueFrontEmpty();
    testPeek();
    testQueueReuse();
    testClear();
    testLargeQueue();
    testIndependentQueues();
    testGenericQueue();

    std::cout << "All queue tests passed.\n";
}