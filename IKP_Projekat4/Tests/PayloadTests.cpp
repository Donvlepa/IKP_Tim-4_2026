#include <iostream>
#include <cstdlib>
#include "Payloads.h"
#include "Constants.h"
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

// Checks protocol field sizes.
static_assert(sizeof(uint64_t) == 8,
    "Invalid identifier size");

static_assert(sizeof(Priority) == 1,
    "Invalid priority size");

static_assert(sizeof(ResponseStatus) == 1,
    "Invalid response status size");

// Tests the registration payloads.
static void testRegistrationPayloads()
{
    RegisterUrPayload user;
    RegisterWrPayload worker;

    (void)user;

    check(worker.workerId == 0,
        "Invalid default worker ID");

    worker.workerId = 10;

    check(worker.workerId == 10,
        "Invalid worker ID");

    check(Constants::REGISTER_UR_PAYLOAD_SIZE == 0,
        "Invalid user registration size");

    check(Constants::REGISTER_WR_PAYLOAD_SIZE == 8,
        "Invalid worker registration size");

    std::cout << "[PASS] Registration payloads test\n";
}

// Tests the existing request model as a payload.
static void testRequestPayload()
{
    uint8_t data[] = { 10, 20, 30 };

    RequestPayload payload;

    payload.clientRequestId = 1;
    payload.priority = Priority::HIGH;
    payload.dataSize = sizeof(data);
    payload.data = data;

    check(payload.clientRequestId == 1,
        "Invalid client request ID");

    check(payload.priority == Priority::HIGH,
        "Invalid priority");

    check(payload.dataSize == 3,
        "Invalid data size");

    check(payload.data == data,
        "Invalid data pointer");

    check(payload.data[2] == 30,
        "Invalid request data");

    std::cout << "[PASS] Request payload test\n";
}

// Tests default assignment values.
static void testDefaultAssignPayload()
{
    AssignPayload payload;

    check(payload.lbRequestId == 0,
        "Invalid default request ID");

    check(payload.attemptId == 0,
        "Invalid default attempt ID");

    check(payload.priority == Priority::LOW,
        "Invalid default priority");

    check(payload.dataSize == 0,
        "Invalid default data size");

    check(payload.data == nullptr,
        "Invalid default data pointer");

    std::cout << "[PASS] Default assignment payload test\n";
}

// Tests assignment payload initialization.
static void testAssignPayload()
{
    uint8_t data[] = { 1, 2, 3, 4 };

    AssignPayload payload;

    payload.lbRequestId = 101;
    payload.attemptId = 2;
    payload.priority = Priority::MEDIUM;
    payload.dataSize = sizeof(data);
    payload.data = data;

    check(payload.lbRequestId == 101,
        "Invalid assignment request ID");

    check(payload.attemptId == 2,
        "Invalid assignment attempt ID");

    check(payload.priority == Priority::MEDIUM,
        "Invalid assignment priority");

    check(payload.dataSize == 4,
        "Invalid assignment data size");

    check(payload.data == data,
        "Invalid assignment data pointer");

    check(payload.data[3] == 4,
        "Invalid assignment data");

    std::cout << "[PASS] Assignment payload test\n";
}

// Tests the empty heartbeat payload.
static void testHeartbeatPayload()
{
    HeartbeatPayload payload;

    (void)payload;

    check(Constants::HEARTBEAT_PAYLOAD_SIZE == 0,
        "Heartbeat payload must be empty");

    std::cout << "[PASS] Heartbeat payload test\n";
}

// Tests default result values.
static void testDefaultResultPayload()
{
    ResultPayload payload;

    check(payload.lbRequestId == 0,
        "Invalid default result request ID");

    check(payload.attemptId == 0,
        "Invalid default result attempt ID");

    check(payload.resultSize == 0,
        "Invalid default result size");

    check(payload.result == nullptr,
        "Invalid default result pointer");

    std::cout << "[PASS] Default result payload test\n";
}

// Tests result payload initialization.
static void testResultPayload()
{
    uint8_t result[] = { 100, 200 };

    ResultPayload payload;

    payload.lbRequestId = 101;
    payload.attemptId = 2;
    payload.resultSize = sizeof(result);
    payload.result = result;

    check(payload.lbRequestId == 101,
        "Invalid result request ID");

    check(payload.attemptId == 2,
        "Invalid result attempt ID");

    check(payload.resultSize == 2,
        "Invalid result size");

    check(payload.result == result,
        "Invalid result pointer");

    check(payload.result[0] == 100,
        "Invalid result data");

    check(payload.result[1] == 200,
        "Invalid result data");

    std::cout << "[PASS] Result payload test\n";
}

// Tests default response values.
static void testDefaultResponsePayload()
{
    ResponsePayload payload;

    check(payload.clientRequestId == 0,
        "Invalid default client request ID");

    check(payload.status == ResponseStatus::SUCCESS,
        "Invalid default response status");

    check(payload.resultSize == 0,
        "Invalid default response size");

    check(payload.result == nullptr,
        "Invalid default response pointer");

    std::cout << "[PASS] Default response payload test\n";
}

// Tests a successful response.
static void testSuccessfulResponse()
{
    uint8_t result[] = { 5, 10, 15 };

    ResponsePayload payload;

    payload.clientRequestId = 7;
    payload.status = ResponseStatus::SUCCESS;
    payload.resultSize = sizeof(result);
    payload.result = result;

    check(payload.clientRequestId == 7,
        "Invalid response request ID");

    check(payload.status == ResponseStatus::SUCCESS,
        "Invalid response status");

    check(payload.resultSize == 3,
        "Invalid response size");

    check(payload.result == result,
        "Invalid response pointer");

    check(payload.result[2] == 15,
        "Invalid response data");

    std::cout << "[PASS] Successful response test\n";
}

// Tests a cancelled response without result data.
static void testCancelledResponse()
{
    ResponsePayload payload;

    payload.clientRequestId = 7;
    payload.status = ResponseStatus::CANCELLED;

    check(payload.clientRequestId == 7,
        "Invalid cancelled request ID");

    check(payload.status == ResponseStatus::CANCELLED,
        "Invalid cancelled status");

    check(payload.resultSize == 0,
        "Cancelled response must have empty data");

    check(payload.result == nullptr,
        "Cancelled response must have null result");

    std::cout << "[PASS] Cancelled response test\n";
}

// Tests fixed payload sizes.
static void testFixedPayloadSizes()
{
    check(Constants::REQUEST_FIXED_SIZE == 9,
        "Invalid request fixed size");

    check(Constants::ASSIGN_FIXED_SIZE == 17,
        "Invalid assignment fixed size");

    check(Constants::RESULT_FIXED_SIZE == 16,
        "Invalid result fixed size");

    check(Constants::RESPONSE_FIXED_SIZE == 9,
        "Invalid response fixed size");

    std::cout << "[PASS] Fixed payload sizes test\n";
}

// Tests maximum allowed data sizes.
static void testMaximumDataSizes()
{
    check(Constants::MAX_MESSAGE_SIZE == 65544,
        "Invalid maximum message size");

    check(Constants::MAX_REQUEST_DATA_SIZE == 65519,
        "Invalid maximum request data size");

    check(Constants::MAX_RESULT_DATA_SIZE == 65520,
        "Invalid maximum result data size");

    check(
        Constants::REQUEST_FIXED_SIZE +
        Constants::MAX_REQUEST_DATA_SIZE
        <= Constants::MAX_PAYLOAD_SIZE,
        "Maximum request exceeds payload size"
    );

    check(
        Constants::ASSIGN_FIXED_SIZE +
        Constants::MAX_REQUEST_DATA_SIZE
        == Constants::MAX_PAYLOAD_SIZE,
        "Invalid maximum assignment size"
    );

    check(
        Constants::RESULT_FIXED_SIZE +
        Constants::MAX_RESULT_DATA_SIZE
        == Constants::MAX_PAYLOAD_SIZE,
        "Invalid maximum result size"
    );

    check(
        Constants::RESPONSE_FIXED_SIZE +
        Constants::MAX_RESULT_DATA_SIZE
        <= Constants::MAX_PAYLOAD_SIZE,
        "Maximum response exceeds payload size"
    );

    std::cout << "[PASS] Maximum data sizes test\n";
}

// Runs all payload model tests.
void runPayloadTests()
{
    testRegistrationPayloads();
    testRequestPayload();
    testDefaultAssignPayload();
    testAssignPayload();
    testHeartbeatPayload();
    testDefaultResultPayload();
    testResultPayload();
    testDefaultResponsePayload();
    testSuccessfulResponse();
    testCancelledResponse();
    testFixedPayloadSizes();
    testMaximumDataSizes();

    std::cout << "All payload model tests passed.\n";
}