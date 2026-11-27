#pragma once

#include <cstdint>

namespace Constants
{
    // Network configuration.
    constexpr uint16_t SERVER_PORT = 5059;

    // Heartbeat configuration.
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;
    constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 3000;

    // Priority scheduling configuration.
    constexpr uint32_t PRIORITY_RATIO = 5;

    // Communication protocol configuration.
    constexpr uint16_t PROTOCOL_VERSION = 1;
    constexpr uint32_t HEADER_SIZE = 8;
    constexpr uint32_t MAX_PAYLOAD_SIZE = 64 * 1024;

    constexpr uint32_t MAX_MESSAGE_SIZE =
        HEADER_SIZE + MAX_PAYLOAD_SIZE;

    // Fixed payload field sizes.
    constexpr uint32_t REGISTER_UR_PAYLOAD_SIZE = 0;
    constexpr uint32_t REGISTER_WR_PAYLOAD_SIZE = 8;
    constexpr uint32_t REQUEST_FIXED_SIZE = 9;
    constexpr uint32_t ASSIGN_FIXED_SIZE = 17;
    constexpr uint32_t HEARTBEAT_PAYLOAD_SIZE = 0;
    constexpr uint32_t RESULT_FIXED_SIZE = 16;
    constexpr uint32_t RESPONSE_FIXED_SIZE = 9;

    // Maximum application data sizes.
    constexpr uint32_t MAX_REQUEST_DATA_SIZE =
        MAX_PAYLOAD_SIZE - ASSIGN_FIXED_SIZE;

    constexpr uint32_t MAX_RESULT_DATA_SIZE =
        MAX_PAYLOAD_SIZE - RESULT_FIXED_SIZE;
}