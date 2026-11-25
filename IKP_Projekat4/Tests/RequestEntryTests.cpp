#include <iostream>
#include <cassert>
#include "RequestEntry.h"
#include "Constants.h"
#include "TestSuites.h"

void testDefaultRequestEntry()
{
    RequestEntry entry;

    assert(entry.lbRequestId == 0);
    assert(entry.clientRequestId == 0);

    assert(entry.priority == Priority::LOW);
    assert(entry.status == RequestStatus::QUEUED);

    assert(entry.attemptId == 0);
    assert(entry.responseSent == false);

    assert(entry.userSocket == INVALID_SOCKET);
    assert(entry.userConnected == false);
    assert(entry.assignedWorkerId == 0);

    assert(entry.getData() == nullptr);
    assert(entry.getDataSize() == 0);

    std::cout << "Default request entry test passed.\n";
}

void testRequestEntryData()
{
    RequestEntry entry;

    uint8_t payload[] = { 10, 20, 30, 40 };

    bool success = entry.setData(payload, sizeof(payload));

    assert(success);
    assert(entry.getDataSize() == 4);

    assert(entry.getData() != payload);

    assert(entry.getData()[0] == 10);
    assert(entry.getData()[1] == 20);
    assert(entry.getData()[2] == 30);
    assert(entry.getData()[3] == 40);

    payload[0] = 99;

    assert(entry.getData()[0] == 10);

    std::cout << "Request entry data test passed.\n";
}

void testRequestEntryCopy()
{
    RequestEntry original;

    original.lbRequestId = 100;
    original.priority = Priority::HIGH;

    uint8_t payload[] = { 1, 2, 3 };

    original.setData(payload, sizeof(payload));

    RequestEntry copy = original;

    assert(copy.lbRequestId == original.lbRequestId);
    assert(copy.priority == original.priority);

    assert(copy.getDataSize() == original.getDataSize());
    assert(copy.getData() != original.getData());

    original.clearData();

    assert(copy.getData()[0] == 1);
    assert(copy.getData()[1] == 2);
    assert(copy.getData()[2] == 3);

    std::cout << "Request entry copy test passed.\n";
}

void testRequestEntryAssignment()
{
    RequestEntry first;
    RequestEntry second;

    uint8_t firstPayload[] = { 10, 20 };
    uint8_t secondPayload[] = { 30, 40, 50 };

    first.setData(firstPayload, sizeof(firstPayload));
    second.setData(secondPayload, sizeof(secondPayload));

    first.lbRequestId = 101;

    second = first;

    assert(second.lbRequestId == 101);
    assert(second.getDataSize() == 2);
    assert(second.getData() != first.getData());

    assert(second.getData()[0] == 10);
    assert(second.getData()[1] == 20);

    second = second;

    assert(second.getDataSize() == 2);
    assert(second.getData()[0] == 10);

    std::cout << "Request entry assignment test passed.\n";
}

void testInvalidRequestEntryData()
{
    RequestEntry entry;

    uint8_t payload[] = { 10, 20 };

    entry.setData(payload, sizeof(payload));

    bool success = entry.setData(nullptr, 10);

    assert(!success);
    assert(entry.getDataSize() == 2);

    success = entry.setData(
        payload,
        Constants::MAX_PAYLOAD_SIZE + 1
    );

    assert(!success);
    assert(entry.getDataSize() == 2);

    std::cout << "Invalid request entry data test passed.\n";
}

void testRequestEntryClear()
{
    RequestEntry entry;

    uint8_t payload[] = { 10, 20 };

    entry.setData(payload, sizeof(payload));

    entry.clearData();

    assert(entry.getData() == nullptr);
    assert(entry.getDataSize() == 0);

    entry.clearData();

    assert(entry.getData() == nullptr);

    std::cout << "Request entry clear test passed.\n";
}

// Runs all request entry tests.
void runRequestEntryTests()
{
    testDefaultRequestEntry();
    testRequestEntryData();
    testRequestEntryCopy();
    testRequestEntryAssignment();
    testInvalidRequestEntryData();
    testRequestEntryClear();
}