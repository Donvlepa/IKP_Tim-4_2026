#include <iostream>
#include "Constants.h"
#include "TestSuites.h"

static_assert(Constants::SERVER_PORT == 5059,
    "Invalid server port");

static_assert(Constants::HEARTBEAT_INTERVAL_MS == 1000,
    "Invalid heartbeat interval");

static_assert(Constants::HEARTBEAT_TIMEOUT_MS == 3000,
    "Invalid heartbeat timeout");

static_assert(Constants::PRIORITY_RATIO == 5,
    "Invalid priority ratio");

static_assert(Constants::PROTOCOL_VERSION == 1,
    "Invalid protocol version");

static_assert(Constants::HEADER_SIZE == 8,
    "Invalid header size");

static_assert(Constants::MAX_PAYLOAD_SIZE == 65536,
    "Invalid maximum payload size");

void runConstantsTests()
{
    std::cout << "All system constant checks passed.\n";
}