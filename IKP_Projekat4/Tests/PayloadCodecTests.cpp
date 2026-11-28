#include <iostream>
#include <cstdlib>
#include <cstring>

#include "PayloadCodec.h"
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

// Tests user registration serialization.
static void testRegisterUr()
{
    RegisterUrPayload payload;
    RegisterUrPayload decoded;

    std::size_t written = 99;

    check(PayloadCodec::serializeRegisterUr(
        payload, nullptr, 0, written),
        "REGISTER_UR serialization failed");

    check(written == 0,
        "REGISTER_UR must contain zero bytes");

    check(PayloadCodec::deserializeRegisterUr(
        nullptr, 0, decoded),
        "REGISTER_UR deserialization failed");

    uint8_t buffer[] = { 1 };

    check(!PayloadCodec::deserializeRegisterUr(
        buffer, sizeof(buffer), decoded),
        "REGISTER_UR accepted unexpected data");

    std::cout << "[PASS] REGISTER_UR codec test\n";
}

// Tests worker registration serialization.
static void testRegisterWr()
{
    RegisterWrPayload payload;
    RegisterWrPayload decoded;

    payload.workerId = 0x0102030405060708ULL;

    uint8_t buffer[9] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeRegisterWr(
        payload, buffer, sizeof(buffer), written),
        "REGISTER_WR serialization failed");

    check(written == 8,
        "Invalid REGISTER_WR payload size");

    const uint8_t expected[] =
    {
        1, 2, 3, 4, 5, 6, 7, 8
    };

    check(std::memcmp(buffer, expected, 8) == 0,
        "Invalid worker ID byte order");

    check(PayloadCodec::deserializeRegisterWr(
        buffer, written, decoded),
        "REGISTER_WR deserialization failed");

    check(decoded.workerId == payload.workerId,
        "Incorrect decoded worker ID");

    check(!PayloadCodec::serializeRegisterWr(
        payload, buffer, 7, written),
        "Small registration buffer accepted");

    check(written == 0,
        "Failed serialization did not reset size");

    check(!PayloadCodec::serializeRegisterWr(
        payload, nullptr, 8, written),
        "Null registration buffer accepted");

    check(!PayloadCodec::deserializeRegisterWr(
        buffer, 7, decoded),
        "Incomplete registration accepted");

    check(!PayloadCodec::deserializeRegisterWr(
        buffer, 9, decoded),
        "Registration with extra data accepted");

    check(!PayloadCodec::deserializeRegisterWr(
        nullptr, 8, decoded),
        "Null registration input accepted");

    std::cout << "[PASS] REGISTER_WR codec test\n";
}

// Tests user request serialization.
static void testRequest()
{
    const uint8_t data[] = { 10, 0, 255 };

    RequestPayload payload;
    RequestPayload decoded;

    payload.clientRequestId = 0x0102030405060708ULL;
    payload.priority = Priority::HIGH;
    payload.dataSize = sizeof(data);
    payload.data = data;

    uint8_t buffer[32] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeRequest(
        payload, buffer, sizeof(buffer), written),
        "REQUEST serialization failed");

    check(written == 12,
        "Invalid REQUEST payload size");

    const uint8_t expected[] =
    {
        1, 2, 3, 4, 5, 6, 7, 8,
        static_cast<uint8_t>(Priority::HIGH),
        10, 0, 255
    };

    check(std::memcmp(
        buffer, expected, sizeof(expected)) == 0,
        "Incorrect REQUEST binary format");

    check(PayloadCodec::deserializeRequest(
        buffer, written, decoded),
        "REQUEST deserialization failed");

    check(decoded.clientRequestId ==
        payload.clientRequestId,
        "Incorrect decoded client request ID");

    check(decoded.priority == payload.priority,
        "Incorrect decoded request priority");

    check(decoded.dataSize == sizeof(data),
        "Incorrect decoded request size");

    check(decoded.data == buffer + 9,
        "Incorrect decoded data pointer");

    check(std::memcmp(
        decoded.data, data, sizeof(data)) == 0,
        "Incorrect decoded request data");

    std::cout << "[PASS] REQUEST codec test\n";
}

// Tests assignment serialization.
static void testAssign()
{
    const uint8_t data[] = { 11, 22, 33, 44 };

    AssignPayload payload;
    AssignPayload decoded;

    payload.lbRequestId = 0x0102030405060708ULL;
    payload.attemptId = 0x1112131415161718ULL;
    payload.priority = Priority::MEDIUM;
    payload.dataSize = sizeof(data);
    payload.data = data;

    uint8_t buffer[64] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeAssign(
        payload, buffer, sizeof(buffer), written),
        "ASSIGN serialization failed");

    check(written == 21,
        "Invalid ASSIGN payload size");

    const uint8_t expected[] =
    {
        1, 2, 3, 4, 5, 6, 7, 8,
        17, 18, 19, 20, 21, 22, 23, 24,
        static_cast<uint8_t>(Priority::MEDIUM),
        11, 22, 33, 44
    };

    check(std::memcmp(
        buffer, expected, sizeof(expected)) == 0,
        "Incorrect ASSIGN binary format");

    check(PayloadCodec::deserializeAssign(
        buffer, written, decoded),
        "ASSIGN deserialization failed");

    check(decoded.lbRequestId == payload.lbRequestId,
        "Incorrect assignment request ID");

    check(decoded.attemptId == payload.attemptId,
        "Incorrect assignment attempt ID");

    check(decoded.priority == payload.priority,
        "Incorrect assignment priority");

    check(decoded.dataSize == sizeof(data),
        "Incorrect assignment data size");

    check(decoded.data == buffer + 17,
        "Incorrect assignment data pointer");

    check(std::memcmp(
        decoded.data, data, sizeof(data)) == 0,
        "Incorrect assignment data");

    std::cout << "[PASS] ASSIGN codec test\n";
}

// Tests empty heartbeat serialization.
static void testHeartbeat()
{
    HeartbeatPayload payload;
    HeartbeatPayload decoded;

    std::size_t written = 99;

    check(PayloadCodec::serializeHeartbeat(
        payload, nullptr, 0, written),
        "HEARTBEAT serialization failed");

    check(written == 0,
        "HEARTBEAT must contain zero bytes");

    check(PayloadCodec::deserializeHeartbeat(
        nullptr, 0, decoded),
        "HEARTBEAT deserialization failed");

    uint8_t buffer[] = { 1 };

    check(!PayloadCodec::deserializeHeartbeat(
        buffer, sizeof(buffer), decoded),
        "HEARTBEAT accepted unexpected data");

    std::cout << "[PASS] HEARTBEAT codec test\n";
}

// Tests result serialization.
static void testResult()
{
    const uint8_t data[] = { 100, 200 };

    ResultPayload payload;
    ResultPayload decoded;

    payload.lbRequestId = 0x0102030405060708ULL;
    payload.attemptId = 2;
    payload.resultSize = sizeof(data);
    payload.result = data;

    uint8_t buffer[32] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeResult(
        payload, buffer, sizeof(buffer), written),
        "RESULT serialization failed");

    check(written == 18,
        "Invalid RESULT payload size");

    const uint8_t expected[] =
    {
        1, 2, 3, 4, 5, 6, 7, 8,
        0, 0, 0, 0, 0, 0, 0, 2,
        100, 200
    };

    check(std::memcmp(
        buffer, expected, sizeof(expected)) == 0,
        "Incorrect RESULT binary format");

    check(PayloadCodec::deserializeResult(
        buffer, written, decoded),
        "RESULT deserialization failed");

    check(decoded.lbRequestId == payload.lbRequestId,
        "Incorrect result request ID");

    check(decoded.attemptId == payload.attemptId,
        "Incorrect result attempt ID");

    check(decoded.resultSize == sizeof(data),
        "Incorrect result data size");

    check(decoded.result == buffer + 16,
        "Incorrect result data pointer");

    check(std::memcmp(
        decoded.result, data, sizeof(data)) == 0,
        "Incorrect result data");

    std::cout << "[PASS] RESULT codec test\n";
}

// Tests successful response serialization.
static void testSuccessfulResponse()
{
    const uint8_t data[] = { 5, 10, 15 };

    ResponsePayload payload;
    ResponsePayload decoded;

    payload.clientRequestId = 7;
    payload.status = ResponseStatus::SUCCESS;
    payload.resultSize = sizeof(data);
    payload.result = data;

    uint8_t buffer[32] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeResponse(
        payload, buffer, sizeof(buffer), written),
        "RESPONSE serialization failed");

    check(written == 12,
        "Invalid RESPONSE payload size");

    const uint8_t expected[] =
    {
        0, 0, 0, 0, 0, 0, 0, 7,
        1, 5, 10, 15
    };

    check(std::memcmp(
        buffer, expected, sizeof(expected)) == 0,
        "Incorrect RESPONSE binary format");

    check(PayloadCodec::deserializeResponse(
        buffer, written, decoded),
        "RESPONSE deserialization failed");

    check(decoded.clientRequestId == 7,
        "Incorrect response request ID");

    check(decoded.status == ResponseStatus::SUCCESS,
        "Incorrect response status");

    check(decoded.resultSize == sizeof(data),
        "Incorrect response result size");

    check(decoded.result == buffer + 9,
        "Incorrect response data pointer");

    check(std::memcmp(
        decoded.result, data, sizeof(data)) == 0,
        "Incorrect response data");

    std::cout << "[PASS] Successful RESPONSE codec test\n";
}

// Tests cancelled response serialization.
static void testCancelledResponse()
{
    ResponsePayload payload;
    ResponsePayload decoded;

    payload.clientRequestId = 15;
    payload.status = ResponseStatus::CANCELLED;

    uint8_t buffer[16] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeResponse(
        payload, buffer, sizeof(buffer), written),
        "Cancelled response serialization failed");

    check(written == 9,
        "Invalid cancelled response size");

    check(buffer[8] == 2,
        "Invalid cancelled response status byte");

    check(PayloadCodec::deserializeResponse(
        buffer, written, decoded),
        "Cancelled response deserialization failed");

    check(decoded.clientRequestId == 15,
        "Invalid cancelled response ID");

    check(decoded.status == ResponseStatus::CANCELLED,
        "Invalid decoded cancelled status");

    check(decoded.resultSize == 0,
        "Cancelled response contains data");

    check(decoded.result == nullptr,
        "Cancelled response contains data pointer");

    uint8_t data[] = { 1 };

    payload.result = data;
    payload.resultSize = sizeof(data);

    check(!PayloadCodec::serializeResponse(
        payload, buffer, sizeof(buffer), written),
        "Cancelled response accepted result data");

    buffer[9] = 1;

    check(!PayloadCodec::deserializeResponse(
        buffer, 10, decoded),
        "Cancelled response accepted extra data");

    std::cout << "[PASS] Cancelled RESPONSE codec test\n";
}

// Tests all supported priorities.
static void testAllPriorities()
{
    const Priority priorities[] =
    {
        Priority::HIGH,
        Priority::MEDIUM,
        Priority::LOW
    };

    RequestPayload request;
    RequestPayload decodedRequest;

    AssignPayload assignment;
    AssignPayload decodedAssignment;

    request.clientRequestId = 1;

    assignment.lbRequestId = 2;
    assignment.attemptId = 1;

    uint8_t buffer[32] = {};
    std::size_t written = 0;

    for (int i = 0; i < 3; ++i)
    {
        request.priority = priorities[i];

        check(PayloadCodec::serializeRequest(
            request, buffer, sizeof(buffer), written),
            "Valid request priority rejected");

        check(PayloadCodec::deserializeRequest(
            buffer, written, decodedRequest),
            "Valid request priority decoding failed");

        check(decodedRequest.priority == priorities[i],
            "Incorrect decoded request priority");

        assignment.priority = priorities[i];

        check(PayloadCodec::serializeAssign(
            assignment, buffer, sizeof(buffer), written),
            "Valid assignment priority rejected");

        check(PayloadCodec::deserializeAssign(
            buffer, written, decodedAssignment),
            "Valid assignment priority decoding failed");

        check(decodedAssignment.priority == priorities[i],
            "Incorrect decoded assignment priority");
    }

    std::cout << "[PASS] All priorities codec test\n";
}

// Tests variable payloads without application data.
static void testEmptyData()
{
    RequestPayload request;
    RequestPayload decodedRequest;

    AssignPayload assignment;
    AssignPayload decodedAssignment;

    ResultPayload result;
    ResultPayload decodedResult;

    ResponsePayload response;
    ResponsePayload decodedResponse;

    request.clientRequestId = 1;

    assignment.lbRequestId = 2;
    assignment.attemptId = 1;

    result.lbRequestId = 2;
    result.attemptId = 1;

    response.clientRequestId = 1;

    uint8_t buffer[32] = {};
    std::size_t written = 0;

    check(PayloadCodec::serializeRequest(
        request, buffer, sizeof(buffer), written),
        "Empty request serialization failed");

    check(written == Constants::REQUEST_FIXED_SIZE,
        "Invalid empty request size");

    check(PayloadCodec::deserializeRequest(
        buffer, written, decodedRequest),
        "Empty request deserialization failed");

    check(decodedRequest.dataSize == 0 &&
        decodedRequest.data == nullptr,
        "Incorrect empty request data");

    check(PayloadCodec::serializeAssign(
        assignment, buffer, sizeof(buffer), written),
        "Empty assignment serialization failed");

    check(written == Constants::ASSIGN_FIXED_SIZE,
        "Invalid empty assignment size");

    check(PayloadCodec::deserializeAssign(
        buffer, written, decodedAssignment),
        "Empty assignment deserialization failed");

    check(decodedAssignment.dataSize == 0 &&
        decodedAssignment.data == nullptr,
        "Incorrect empty assignment data");

    check(PayloadCodec::serializeResult(
        result, buffer, sizeof(buffer), written),
        "Empty result serialization failed");

    check(written == Constants::RESULT_FIXED_SIZE,
        "Invalid empty result size");

    check(PayloadCodec::deserializeResult(
        buffer, written, decodedResult),
        "Empty result deserialization failed");

    check(decodedResult.resultSize == 0 &&
        decodedResult.result == nullptr,
        "Incorrect empty result data");

    check(PayloadCodec::serializeResponse(
        response, buffer, sizeof(buffer), written),
        "Empty response serialization failed");

    check(written == Constants::RESPONSE_FIXED_SIZE,
        "Invalid empty response size");

    check(PayloadCodec::deserializeResponse(
        buffer, written, decodedResponse),
        "Empty response deserialization failed");

    check(decodedResponse.resultSize == 0 &&
        decodedResponse.result == nullptr,
        "Incorrect empty response data");

    std::cout << "[PASS] Empty application data test\n";
}

// Tests invalid priority and response status values.
static void testInvalidEnumValues()
{
    RequestPayload request;
    AssignPayload assignment;
    ResponsePayload response;

    request.priority = static_cast<Priority>(255);
    assignment.priority = static_cast<Priority>(255);
    response.status = static_cast<ResponseStatus>(255);

    uint8_t buffer[32] = {};
    std::size_t written = 99;

    check(!PayloadCodec::serializeRequest(
        request, buffer, sizeof(buffer), written),
        "Invalid request priority accepted");

    check(!PayloadCodec::serializeAssign(
        assignment, buffer, sizeof(buffer), written),
        "Invalid assignment priority accepted");

    check(!PayloadCodec::serializeResponse(
        response, buffer, sizeof(buffer), written),
        "Invalid response status accepted");

    check(written == 0,
        "Invalid enum serialization changed written size");

    buffer[8] = 255;
    buffer[16] = 255;

    check(!PayloadCodec::deserializeRequest(
        buffer, Constants::REQUEST_FIXED_SIZE, request),
        "Invalid encoded request priority accepted");

    check(!PayloadCodec::deserializeAssign(
        buffer, Constants::ASSIGN_FIXED_SIZE, assignment),
        "Invalid encoded assignment priority accepted");

    check(!PayloadCodec::deserializeResponse(
        buffer, Constants::RESPONSE_FIXED_SIZE, response),
        "Invalid encoded response status accepted");

    std::cout << "[PASS] Invalid enum values test\n";
}

// Tests insufficient buffer capacities.
static void testSmallBuffers()
{
    const uint8_t data[] = { 1 };

    RequestPayload request;
    AssignPayload assignment;
    ResultPayload result;
    ResponsePayload response;

    request.data = data;
    request.dataSize = sizeof(data);

    assignment.data = data;
    assignment.dataSize = sizeof(data);

    result.result = data;
    result.resultSize = sizeof(data);

    response.result = data;
    response.resultSize = sizeof(data);

    uint8_t buffer[32] = {};
    std::size_t written = 99;

    check(!PayloadCodec::serializeRequest(
        request, buffer,
        Constants::REQUEST_FIXED_SIZE, written),
        "Small request buffer accepted");

    check(!PayloadCodec::serializeAssign(
        assignment, buffer,
        Constants::ASSIGN_FIXED_SIZE, written),
        "Small assignment buffer accepted");

    check(!PayloadCodec::serializeResult(
        result, buffer,
        Constants::RESULT_FIXED_SIZE, written),
        "Small result buffer accepted");

    check(!PayloadCodec::serializeResponse(
        response, buffer,
        Constants::RESPONSE_FIXED_SIZE, written),
        "Small response buffer accepted");

    check(written == 0,
        "Invalid bytes written after serialization failure");

    check(!PayloadCodec::deserializeRequest(
        buffer, Constants::REQUEST_FIXED_SIZE - 1, request),
        "Incomplete request accepted");

    check(!PayloadCodec::deserializeAssign(
        buffer, Constants::ASSIGN_FIXED_SIZE - 1, assignment),
        "Incomplete assignment accepted");

    check(!PayloadCodec::deserializeResult(
        buffer, Constants::RESULT_FIXED_SIZE - 1, result),
        "Incomplete result accepted");

    check(!PayloadCodec::deserializeResponse(
        buffer, Constants::RESPONSE_FIXED_SIZE - 1, response),
        "Incomplete response accepted");

    std::cout << "[PASS] Insufficient buffer test\n";
}

// Tests null pointers.
static void testNullPointers()
{
    RequestPayload request;
    AssignPayload assignment;
    ResultPayload result;
    ResponsePayload response;

    uint8_t buffer[32] = {};
    std::size_t written = 0;

    check(!PayloadCodec::serializeRequest(
        request, nullptr, sizeof(buffer), written),
        "Null request output buffer accepted");

    check(!PayloadCodec::serializeAssign(
        assignment, nullptr, sizeof(buffer), written),
        "Null assignment output buffer accepted");

    check(!PayloadCodec::serializeResult(
        result, nullptr, sizeof(buffer), written),
        "Null result output buffer accepted");

    check(!PayloadCodec::serializeResponse(
        response, nullptr, sizeof(buffer), written),
        "Null response output buffer accepted");

    request.dataSize = 1;
    assignment.dataSize = 1;
    result.resultSize = 1;
    response.resultSize = 1;

    check(!PayloadCodec::serializeRequest(
        request, buffer, sizeof(buffer), written),
        "Null request data accepted");

    check(!PayloadCodec::serializeAssign(
        assignment, buffer, sizeof(buffer), written),
        "Null assignment data accepted");

    check(!PayloadCodec::serializeResult(
        result, buffer, sizeof(buffer), written),
        "Null result data accepted");

    check(!PayloadCodec::serializeResponse(
        response, buffer, sizeof(buffer), written),
        "Null response data accepted");

    check(!PayloadCodec::deserializeRequest(
        nullptr, Constants::REQUEST_FIXED_SIZE, request),
        "Null request input accepted");

    check(!PayloadCodec::deserializeAssign(
        nullptr, Constants::ASSIGN_FIXED_SIZE, assignment),
        "Null assignment input accepted");

    check(!PayloadCodec::deserializeResult(
        nullptr, Constants::RESULT_FIXED_SIZE, result),
        "Null result input accepted");

    check(!PayloadCodec::deserializeResponse(
        nullptr, Constants::RESPONSE_FIXED_SIZE, response),
        "Null response input accepted");

    std::cout << "[PASS] Null pointer test\n";
}

// Tests maximum supported application data sizes.
static void testMaximumPayloads()
{
    static uint8_t data[Constants::MAX_RESULT_DATA_SIZE];
    static uint8_t buffer[Constants::MAX_PAYLOAD_SIZE];

    for (uint32_t i = 0;
        i < Constants::MAX_RESULT_DATA_SIZE;
        ++i)
    {
        data[i] = static_cast<uint8_t>(i % 256);
    }

    std::size_t written = 0;

    RequestPayload request;
    RequestPayload decodedRequest;

    request.clientRequestId = 1;
    request.priority = Priority::HIGH;
    request.data = data;
    request.dataSize = Constants::MAX_REQUEST_DATA_SIZE;

    check(PayloadCodec::serializeRequest(
        request, buffer, sizeof(buffer), written),
        "Maximum request serialization failed");

    check(written == Constants::REQUEST_FIXED_SIZE +
        Constants::MAX_REQUEST_DATA_SIZE,
        "Invalid maximum request size");

    check(PayloadCodec::deserializeRequest(
        buffer, written, decodedRequest),
        "Maximum request deserialization failed");

    check(decodedRequest.dataSize == request.dataSize,
        "Invalid maximum decoded request size");

    check(std::memcmp(
        decodedRequest.data, data, request.dataSize) == 0,
        "Maximum request data mismatch");

    AssignPayload assignment;
    AssignPayload decodedAssignment;

    assignment.lbRequestId = 1;
    assignment.attemptId = 1;
    assignment.priority = Priority::MEDIUM;
    assignment.data = data;
    assignment.dataSize = Constants::MAX_REQUEST_DATA_SIZE;

    check(PayloadCodec::serializeAssign(
        assignment, buffer, sizeof(buffer), written),
        "Maximum assignment serialization failed");

    check(written == Constants::MAX_PAYLOAD_SIZE,
        "Invalid maximum assignment size");

    check(PayloadCodec::deserializeAssign(
        buffer, written, decodedAssignment),
        "Maximum assignment deserialization failed");

    check(decodedAssignment.dataSize ==
        assignment.dataSize,
        "Invalid maximum decoded assignment size");

    check(std::memcmp(
        decodedAssignment.data, data,
        assignment.dataSize) == 0,
        "Maximum assignment data mismatch");

    ResultPayload result;
    ResultPayload decodedResult;

    result.lbRequestId = 1;
    result.attemptId = 1;
    result.result = data;
    result.resultSize = Constants::MAX_RESULT_DATA_SIZE;

    check(PayloadCodec::serializeResult(
        result, buffer, sizeof(buffer), written),
        "Maximum result serialization failed");

    check(written == Constants::MAX_PAYLOAD_SIZE,
        "Invalid maximum result payload size");

    check(PayloadCodec::deserializeResult(
        buffer, written, decodedResult),
        "Maximum result deserialization failed");

    check(decodedResult.resultSize == result.resultSize,
        "Invalid maximum decoded result size");

    check(std::memcmp(
        decodedResult.result, data,
        result.resultSize) == 0,
        "Maximum result data mismatch");

    ResponsePayload response;
    ResponsePayload decodedResponse;

    response.clientRequestId = 1;
    response.status = ResponseStatus::SUCCESS;
    response.result = data;
    response.resultSize = Constants::MAX_RESULT_DATA_SIZE;

    check(PayloadCodec::serializeResponse(
        response, buffer, sizeof(buffer), written),
        "Maximum response serialization failed");

    check(written == Constants::RESPONSE_FIXED_SIZE +
        Constants::MAX_RESULT_DATA_SIZE,
        "Invalid maximum response size");

    check(PayloadCodec::deserializeResponse(
        buffer, written, decodedResponse),
        "Maximum response deserialization failed");

    check(decodedResponse.resultSize ==
        response.resultSize,
        "Invalid maximum decoded response size");

    check(std::memcmp(
        decodedResponse.result, data,
        response.resultSize) == 0,
        "Maximum response data mismatch");

    std::cout << "[PASS] Maximum payload sizes test\n";
}

// Tests payloads exceeding configured limits.
static void testOversizedPayloads()
{
    static uint8_t buffer[
        Constants::MAX_PAYLOAD_SIZE + 1
    ] = {};

        static uint8_t data[
            Constants::MAX_RESULT_DATA_SIZE + 1
        ] = {};

            RequestPayload request;
            AssignPayload assignment;
            ResultPayload result;
            ResponsePayload response;

            request.data = data;
            request.dataSize = Constants::MAX_REQUEST_DATA_SIZE + 1;

            assignment.data = data;
            assignment.dataSize =
                Constants::MAX_REQUEST_DATA_SIZE + 1;

            result.result = data;
            result.resultSize = Constants::MAX_RESULT_DATA_SIZE + 1;

            response.result = data;
            response.resultSize =
                Constants::MAX_RESULT_DATA_SIZE + 1;

            std::size_t written = 99;

            check(!PayloadCodec::serializeRequest(
                request, buffer, sizeof(buffer), written),
                "Oversized request accepted");

            check(!PayloadCodec::serializeAssign(
                assignment, buffer, sizeof(buffer), written),
                "Oversized assignment accepted");

            check(!PayloadCodec::serializeResult(
                result, buffer, sizeof(buffer), written),
                "Oversized result accepted");

            check(!PayloadCodec::serializeResponse(
                response, buffer, sizeof(buffer), written),
                "Oversized response accepted");

            buffer[8] = static_cast<uint8_t>(Priority::HIGH);
            buffer[16] = static_cast<uint8_t>(Priority::HIGH);

            check(!PayloadCodec::deserializeRequest(
                buffer,
                Constants::REQUEST_FIXED_SIZE +
                Constants::MAX_REQUEST_DATA_SIZE + 1,
                request),
                "Oversized encoded request accepted");

            check(!PayloadCodec::deserializeAssign(
                buffer, sizeof(buffer), assignment),
                "Oversized encoded assignment accepted");

            check(!PayloadCodec::deserializeResult(
                buffer, sizeof(buffer), result),
                "Oversized encoded result accepted");

            buffer[8] =
                static_cast<uint8_t>(ResponseStatus::SUCCESS);

            check(!PayloadCodec::deserializeResponse(
                buffer,
                Constants::RESPONSE_FIXED_SIZE +
                Constants::MAX_RESULT_DATA_SIZE + 1,
                response),
                "Oversized encoded response accepted");

            check(written == 0,
                "Invalid size after oversized payload rejection");

            std::cout << "[PASS] Oversized payload rejection test\n";
}

// Tests state preservation on codec failures.
static void testFailurePreservesState()
{
    const uint8_t data[] = { 7, 8, 9 };

    RequestPayload request;

    request.clientRequestId = 123;
    request.priority = Priority::HIGH;
    request.data = data;
    request.dataSize = sizeof(data);

    uint8_t buffer[32];

    std::memset(buffer, 0xAA, sizeof(buffer));

    std::size_t written = 99;

    check(!PayloadCodec::serializeRequest(
        request, buffer, 9, written),
        "Invalid serialization succeeded");

    check(written == 0,
        "Failed serialization did not reset size");

    for (std::size_t i = 0; i < sizeof(buffer); ++i)
    {
        check(buffer[i] == 0xAA,
            "Failed serialization modified output buffer");
    }

    buffer[8] = 255;

    check(!PayloadCodec::deserializeRequest(
        buffer, 12, request),
        "Invalid deserialization succeeded");

    check(request.clientRequestId == 123,
        "Failed decoding changed request ID");

    check(request.priority == Priority::HIGH,
        "Failed decoding changed priority");

    check(request.dataSize == sizeof(data),
        "Failed decoding changed data size");

    check(request.data == data,
        "Failed decoding changed data pointer");

    std::cout << "[PASS] Failure state preservation test\n";
}

// Runs all payload codec tests.
void runPayloadCodecTests()
{
    testRegisterUr();
    testRegisterWr();
    testRequest();
    testAssign();
    testHeartbeat();
    testResult();
    testSuccessfulResponse();
    testCancelledResponse();
    testAllPriorities();
    testEmptyData();
    testInvalidEnumValues();
    testSmallBuffers();
    testNullPointers();
    testMaximumPayloads();
    testOversizedPayloads();
    testFailurePreservesState();

    std::cout << "All payload codec tests passed.\n";
}