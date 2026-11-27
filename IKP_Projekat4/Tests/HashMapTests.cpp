#include <iostream>
#include <cstdlib>
#include <cstdint>
#include "HashMap.h"
#include "RequestEntry.h"
#include "WorkerEntry.h"
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

// Tests initialization of an empty hash map.
static void testEmptyHashMap()
{
    HashMap<int> map;

    check(map.isValid(), "Map initialization failed");
    check(map.isEmpty(), "Map must be empty");
    check(map.size() == 0, "Initial size must be zero");
    check(map.capacity() == 16, "Invalid initial capacity");

    check(map.find(1) == nullptr, "Unexpected key found");
    check(!map.contains(1), "Unexpected existing key");
    check(!map.remove(1), "Empty remove must fail");

    std::cout << "[PASS] Empty hash map test\n";
}

// Tests insertion and lookup.
static void testInsertAndFind()
{
    HashMap<int> map;

    check(map.insert(1, 100), "Insert 1 failed");
    check(map.insert(2, 200), "Insert 2 failed");
    check(map.insert(3, 300), "Insert 3 failed");

    check(map.size() == 3, "Invalid map size");

    check(map.find(1) != nullptr, "Key 1 not found");
    check(map.find(2) != nullptr, "Key 2 not found");
    check(map.find(3) != nullptr, "Key 3 not found");

    check(*map.find(1) == 100, "Invalid value for key 1");
    check(*map.find(2) == 200, "Invalid value for key 2");
    check(*map.find(3) == 300, "Invalid value for key 3");

    check(map.contains(2), "Existing key not found");
    check(!map.contains(99), "Unexpected key found");

    std::cout << "[PASS] Insert and find test\n";
}

// Tests duplicate key rejection.
static void testDuplicateKey()
{
    HashMap<int> map;

    check(map.insert(1, 100), "Insert failed");
    check(!map.insert(1, 999), "Duplicate key accepted");

    check(map.size() == 1, "Duplicate changed map size");
    check(*map.find(1) == 100, "Original value changed");

    std::cout << "[PASS] Duplicate key test\n";
}

// Tests updating existing values.
static void testUpdate()
{
    HashMap<int> map;

    check(map.insert(1, 100), "Insert failed");
    check(map.update(1, 500), "Update failed");

    check(*map.find(1) == 500, "Invalid updated value");
    check(map.size() == 1, "Update changed map size");

    check(!map.update(99, 100), "Missing key updated");

    std::cout << "[PASS] Update test\n";
}

// Tests collision handling using separate chaining.
static void testCollisions()
{
    HashMap<int> map(8);

    // All three keys belong to bucket 1.
    check(map.insert(1, 100), "Insert 1 failed");
    check(map.insert(9, 900), "Insert 9 failed");
    check(map.insert(17, 1700), "Insert 17 failed");

    check(map.capacity() == 8, "Unexpected rehash");
    check(map.size() == 3, "Invalid collision map size");

    check(map.find(1) != nullptr, "Key 1 not found");
    check(map.find(9) != nullptr, "Key 9 not found");
    check(map.find(17) != nullptr, "Key 17 not found");

    check(*map.find(1) == 100, "Invalid collision value");
    check(*map.find(9) == 900, "Invalid collision value");
    check(*map.find(17) == 1700, "Invalid collision value");

    std::cout << "[PASS] Collision handling test\n";
}

// Tests removing elements from a collision chain.
static void testRemoveCollisions()
{
    HashMap<int> map(8);

    check(map.insert(1, 100), "Insert failed");
    check(map.insert(9, 900), "Insert failed");
    check(map.insert(17, 1700), "Insert failed");
    check(map.insert(25, 2500), "Insert failed");

    // Chain: 25 -> 17 -> 9 -> 1

    // Remove the middle node.
    check(map.remove(17), "Middle removal failed");
    check(!map.contains(17), "Removed key still exists");

    check(map.contains(25), "First node lost");
    check(map.contains(9), "Middle node lost");
    check(map.contains(1), "Last node lost");

    // Remove the first node.
    check(map.remove(25), "First removal failed");
    check(!map.contains(25), "Removed first node exists");

    // Remove the last node.
    check(map.remove(1), "Last removal failed");
    check(!map.contains(1), "Removed last node exists");

    check(map.size() == 1, "Invalid remaining size");
    check(*map.find(9) == 900, "Remaining value changed");

    check(map.remove(9), "Final removal failed");
    check(map.isEmpty(), "Map must be empty");

    std::cout << "[PASS] Collision removal test\n";
}

// Tests removal of a non-existing key.
static void testRemoveMissingKey()
{
    HashMap<int> map;

    check(map.insert(1, 100), "Insert failed");
    check(!map.remove(99), "Missing key removed");

    check(map.size() == 1, "Map size changed");
    check(*map.find(1) == 100, "Existing value changed");

    std::cout << "[PASS] Missing key removal test\n";
}

// Tests automatic capacity expansion.
static void testRehash()
{
    HashMap<int> map(4);

    check(map.insert(1, 10), "Insert failed");
    check(map.insert(2, 20), "Insert failed");
    check(map.insert(3, 30), "Insert failed");

    check(map.capacity() == 4, "Unexpected capacity");

    int* originalAddress = map.find(1);

    check(originalAddress != nullptr, "Key 1 not found");

    // Triggers capacity expansion.
    check(map.insert(4, 40), "Insert after rehash failed");

    check(map.capacity() == 8, "Capacity did not expand");
    check(map.size() == 4, "Rehash changed map size");

    check(map.find(1) == originalAddress,
        "Rehash changed node address");

    for (uint64_t i = 1; i <= 4; i++)
    {
        int* value = map.find(i);

        check(value != nullptr, "Key lost during rehash");

        check(*value == static_cast<int>(i * 10),
            "Value changed during rehash");
    }

    std::cout << "[PASS] Automatic rehash test\n";
}

// Tests traversal of all stored elements.
static void testForEach()
{
    HashMap<int> map;

    check(map.insert(1, 10), "Insert failed");
    check(map.insert(2, 20), "Insert failed");
    check(map.insert(3, 30), "Insert failed");

    int visited = 0;
    int sum = 0;

    const HashMap<int>& readOnlyMap = map;

    readOnlyMap.forEach(
        [&](uint64_t key, const int& value)
        {
            check(key >= 1 && key <= 3, "Invalid key");

            visited++;
            sum += value;
        }
    );

    check(visited == 3, "Invalid number of visited nodes");
    check(sum == 60, "Invalid sum of values");

    map.forEach(
        [](uint64_t, int& value)
        {
            value *= 2;
        }
    );

    check(*map.find(1) == 20, "Invalid modified value");
    check(*map.find(2) == 40, "Invalid modified value");
    check(*map.find(3) == 60, "Invalid modified value");

    std::cout << "[PASS] Hash map traversal test\n";
}

// Tests clearing and reusing the map.
static void testClear()
{
    HashMap<int> map;

    for (uint64_t i = 1; i <= 100; i++)
    {
        check(map.insert(i, static_cast<int>(i)),
            "Insert failed");
    }

    std::size_t oldCapacity = map.capacity();

    map.clear();

    check(map.isEmpty(), "Clear did not empty map");
    check(map.size() == 0, "Clear did not reset size");

    check(map.capacity() == oldCapacity,
        "Clear changed capacity");

    check(map.find(50) == nullptr, "Cleared key exists");

    map.clear();

    check(map.insert(500, 1000), "Reuse after clear failed");
    check(*map.find(500) == 1000, "Invalid reused value");

    std::cout << "[PASS] Clear and reuse test\n";
}

// Tests invalid initial capacity.
static void testInvalidCapacity()
{
    HashMap<int> map(0);

    check(!map.isValid(), "Zero capacity accepted");
    check(map.isEmpty(), "Invalid map must be empty");

    check(!map.insert(1, 100), "Invalid map accepted insert");
    check(map.find(1) == nullptr, "Invalid map lookup failed");
    check(!map.remove(1), "Invalid map accepted removal");

    map.clear();

    std::cout << "[PASS] Invalid capacity test\n";
}

// Tests support for the full key type.
static void testKeyBoundaries()
{
    HashMap<int> map;

    uint64_t maximumKey = UINT64_MAX;

    check(map.insert(0, 10), "Zero key insertion failed");
    check(map.insert(maximumKey, 20), "Maximum key failed");

    check(map.find(0) != nullptr, "Zero key not found");
    check(map.find(maximumKey) != nullptr,
        "Maximum key not found");

    check(*map.find(0) == 10, "Invalid zero key value");
    check(*map.find(maximumKey) == 20,
        "Invalid maximum key value");

    std::cout << "[PASS] Key boundary test\n";
}

// Tests a large number of elements.
static void testLargeHashMap()
{
    HashMap<int> map;

    const uint64_t numberOfElements = 10000;

    for (uint64_t i = 1; i <= numberOfElements; i++)
    {
        check(map.insert(i, static_cast<int>(i * 2)),
            "Large map insert failed");
    }

    check(map.size() == numberOfElements,
        "Invalid large map size");

    for (uint64_t i = 1; i <= numberOfElements; i++)
    {
        int* value = map.find(i);

        check(value != nullptr, "Large map key not found");

        check(*value == static_cast<int>(i * 2),
            "Invalid large map value");
    }

    for (uint64_t i = 1; i <= numberOfElements; i++)
    {
        check(map.remove(i), "Large map removal failed");
    }

    check(map.isEmpty(), "Large map must be empty");
    check(map.size() == 0, "Invalid final map size");

    std::cout << "[PASS] Large hash map test (10000 elements)\n";
}

// Tests storage of request entries.
static void testRequestRegistry()
{
    HashMap<RequestEntry> registry;

    RequestEntry request;

    request.lbRequestId = 101;
    request.clientRequestId = 1;
    request.priority = Priority::HIGH;

    uint8_t payload[] = { 10, 20, 30 };

    check(request.setData(payload, sizeof(payload)),
        "Request payload initialization failed");

    check(registry.insert(request.lbRequestId, request),
        "Request registry insert failed");

    RequestEntry* stored = registry.find(101);

    check(stored != nullptr, "Request not found");

    check(stored->clientRequestId == 1,
        "Invalid client request ID");

    check(stored->priority == Priority::HIGH,
        "Invalid request priority");

    check(stored->getData() != request.getData(),
        "Request payload was not independently copied");

    request.clearData();

    check(stored->getDataSize() == 3, "Payload size changed");
    check(stored->getData()[0] == 10, "Payload lost");
    check(stored->getData()[1] == 20, "Payload lost");
    check(stored->getData()[2] == 30, "Payload lost");

    stored->status = RequestStatus::IN_PROGRESS;
    stored->attemptId = 1;

    check(registry.find(101)->status ==
        RequestStatus::IN_PROGRESS, "Status update failed");

    check(registry.find(101)->attemptId == 1,
        "Attempt update failed");

    check(registry.remove(101), "Request removal failed");

    std::cout << "[PASS] Request registry test\n";
}

// Tests storage of worker entries.
static void testWorkerRegistry()
{
    HashMap<WorkerEntry> registry;

    WorkerEntry worker;

    worker.workerId = 1;
    worker.sessionId = 101;
    worker.status = WorkerStatus::FREE;
    worker.lastHeartbeatMs = 10000;

    check(registry.insert(worker.workerId, worker),
        "Worker registry insert failed");

    WorkerEntry* stored = registry.find(1);

    check(stored != nullptr, "Worker not found");

    check(stored->sessionId == 101, "Invalid session ID");

    check(stored->status == WorkerStatus::FREE,
        "Invalid worker status");

    stored->status = WorkerStatus::BUSY;
    stored->currentRequestId = 205;
    stored->lastHeartbeatMs = 11000;

    check(registry.find(1)->status == WorkerStatus::BUSY,
        "Worker status update failed");

    check(registry.find(1)->currentRequestId == 205,
        "Worker assignment update failed");

    check(registry.find(1)->lastHeartbeatMs == 11000,
        "Heartbeat update failed");

    std::cout << "[PASS] Worker registry test\n";
}

// Runs all hash map tests.
void runHashMapTests()
{
    testEmptyHashMap();
    testInsertAndFind();
    testDuplicateKey();
    testUpdate();
    testCollisions();
    testRemoveCollisions();
    testRemoveMissingKey();
    testRehash();
    testForEach();
    testClear();
    testInvalidCapacity();
    testKeyBoundaries();
    testLargeHashMap();
    testRequestRegistry();
    testWorkerRegistry();

    std::cout << "All hash map tests passed.\n";
}