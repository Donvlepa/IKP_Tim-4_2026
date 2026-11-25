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
}