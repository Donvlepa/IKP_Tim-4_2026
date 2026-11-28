#include "pch.h"
#include <winsock2.h>

#include <iostream>
#include <cstdint>
#include <cstddef>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "MessageIO.h"
#include "Payloads.h"
#include "Constants.h"

namespace
{
    constexpr const char* SERVER_ADDRESS =
        "127.0.0.1";

    // Parses a positive 64-bit worker identifier.
    // Rejects invalid characters and integer overflow.
    bool parseWorkerId(
        const char* argument,
        uint64_t& workerId
    )
    {
        if (argument == nullptr ||
            argument[0] == '\0')
        {
            return false;
        }

        uint64_t value = 0;

        for (const char* current = argument;
            *current != '\0';
            ++current)
        {
            if (*current < '0' ||
                *current > '9')
            {
                return false;
            }

            const uint64_t digit =
                static_cast<uint64_t>(
                    *current - '0'
                    );

            // Prevent unsigned integer overflow.
            if (value > (UINT64_MAX - digit) / 10)
            {
                return false;
            }

            value = value * 10 + digit;
        }

        if (value == 0)
        {
            return false;
        }

        workerId = value;

        return true;
    }
}

int main(int argc, char* argv[])
{
    std::cout
        << "================================\n"
        << "             WORKER             \n"
        << "================================\n\n";

    // STEP 1: Read the worker identifier.
    uint64_t workerId = 1;

    if (argc > 2 ||
        (argc == 2 &&
            !parseWorkerId(argv[1], workerId)))
    {
        std::cerr
            << "[ERROR] Invalid worker identifier.\n"
            << "Usage: Worker.exe [workerId]\n";

        return 1;
    }

    std::cout
        << "[WR] Worker ID: "
        << workerId << "\n";

    // STEP 2: Initialize Winsock.
    WinsockContext winsock;

    int result = winsock.initialize();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Winsock initialization failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[WR] Winsock initialized successfully.\n";

    // STEP 3: Connect to the load balancer.
    TcpClient client;

    std::cout
        << "[WR] Connecting to "
        << SERVER_ADDRESS << ":"
        << Constants::SERVER_PORT << "...\n";

    result = client.connectTo(
        SERVER_ADDRESS,
        Constants::SERVER_PORT
    );

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Connection failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[WR] Connected to Load Balancer.\n";

    // STEP 4: Prepare the registration message.
    RegisterWrPayload registration;

    registration.workerId = workerId;

    std::size_t bytesSent = 0;

    // STEP 5: Send the worker registration message.
    result = MessageIO::sendMessage(
        client.getSocket(),
        registration,
        bytesSent
    );

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Worker registration sending failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[WR] REGISTER_WR sent successfully.\n"
        << "[WR] Registration bytes: "
        << bytesSent << "\n";

    // STEP 6: Keep the connection open.
    // Processing and heartbeat threads will be added later.
    std::cout
        << "\n[WR] Worker connection established.\n"
        << "[WR] Request processing is not enabled yet.\n"
        << "[WR] Heartbeat sending is not enabled yet.\n"
        << "[WR] Press Enter to disconnect.\n";

    std::cin.get();

    // STEP 7: Close the worker connection.
    result = client.disconnect();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Worker cleanup failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[WR] Disconnected from Load Balancer.\n";

    // STEP 8: Release Winsock resources.
    result = winsock.cleanup();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Winsock cleanup failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[WR] Winsock resources released.\n"
        << "[WR] Worker stopped successfully.\n";

    return 0;
}