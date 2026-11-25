#include <iostream>
#include <cassert>
#include "Request.h"
#include "TestSuites.h"

// Tests default request initialization.
static void testDefaultRequest()
{
    Request request;

    assert(request.clientRequestId == 0);
    assert(request.priority == Priority::LOW);
    assert(request.dataSize == 0);
    assert(request.data == nullptr);

    std::cout << "Default request test passed.\n";
}

// Tests request initialization with custom data.
static void testRequestInitialization()
{
    uint8_t payload[] = { 10, 20, 30, 40 };

    Request request;

    request.clientRequestId = 1;
    request.priority = Priority::HIGH;
    request.dataSize = sizeof(payload);
    request.data = payload;

    assert(request.clientRequestId == 1);
    assert(request.priority == Priority::HIGH);
    assert(request.dataSize == 4);
    assert(request.data != nullptr);

    assert(request.data[0] == 10);
    assert(request.data[1] == 20);
    assert(request.data[2] == 30);
    assert(request.data[3] == 40);

    std::cout << "Request initialization test passed.\n";
}

// Runs all request model tests.
void runRequestTests()
{
    testDefaultRequest();
    testRequestInitialization();
}