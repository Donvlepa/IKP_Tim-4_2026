#include "pch.h"
#include <winsock2.h>

#include <iostream>
#include <cstring>
#include <cstdint>

#include "WinsockContext.h"
#include "TcpClient.h"
#include "MessageIO.h"
#include "Payloads.h"
#include "Constants.h"

namespace
{
    constexpr const char* SERVER_ADDRESS =
        "127.0.0.1";

    // Converts a command-line argument into a request priority.
    bool parsePriority(
        const char* argument,
        Priority& priority
    )
    {
        if (std::strcmp(argument, "high") == 0)
        {
            priority = Priority::HIGH;

            return true;
        }

        if (std::strcmp(argument, "medium") == 0)
        {
            priority = Priority::MEDIUM;

            return true;
        }

        if (std::strcmp(argument, "low") == 0)
        {
            priority = Priority::LOW;

            return true;
        }

        return false;
    }

    // Returns the display name of a priority.
    const char* getPriorityName(Priority priority)
    {
        switch (priority)
        {
        case Priority::HIGH:
            return "HIGH";

        case Priority::MEDIUM:
            return "MEDIUM";

        case Priority::LOW:
            return "LOW";

        default:
            return "UNKNOWN";
        }
    }
}

int main(int argc, char* argv[])
{
    std::cout
        << "================================\n"
        << "              USER              \n"
        << "================================\n\n";

    // STEP 1: Select the request priority.
    Priority priority = Priority::HIGH;

    if (argc > 2 ||
        (argc == 2 &&
            !parsePriority(argv[1], priority)))
    {
        std::cerr
            << "Usage: User.exe [high|medium|low]\n";

        return 1;
    }

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
        << "[UR] Winsock initialized successfully.\n";

    // STEP 3: Connect to the load balancer.
    TcpClient client;

    std::cout
        << "[UR] Connecting to "
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
        << "[UR] Connected to Load Balancer.\n";

    // STEP 4: Send the user registration message.
    RegisterUrPayload registration;

    std::size_t bytesSent = 0;

    result = MessageIO::sendMessage(
        client.getSocket(),
        registration,
        bytesSent
    );

    if (result != 0)
    {
        std::cerr
            << "[ERROR] User registration sending failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[UR] REGISTER_UR sent successfully.\n"
        << "[UR] Registration bytes: "
        << bytesSent << "\n";

    // STEP 5: Prepare a sample user request.
    const uint8_t requestData[] =
    {
        10, 20, 30, 40, 50
    };

    RequestPayload request;

    request.clientRequestId = 1;

    request.priority = priority;

    request.dataSize =
        static_cast<uint32_t>(
            sizeof(requestData)
            );

    request.data = requestData;

    std::cout
        << "\n[UR] Preparing request...\n"
        << "[UR] Client request ID: "
        << request.clientRequestId << "\n"
        << "[UR] Priority: "
        << getPriorityName(request.priority) << "\n"
        << "[UR] Data size: "
        << request.dataSize << " bytes\n";

    // STEP 6: Send the complete request.
    result = MessageIO::sendMessage(
        client.getSocket(),
        request,
        bytesSent
    );

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Request sending failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[UR] REQUEST sent successfully.\n"
        << "[UR] Total request bytes: "
        << bytesSent << "\n";

    // Response processing will be implemented later.
    std::cout
        << "\n[UR] Response processing is not enabled yet.\n"
        << "[UR] Press Enter to disconnect.\n";

    std::cin.get();

    // STEP 7: Close the client connection.
    result = client.disconnect();

    if (result != 0)
    {
        std::cerr
            << "[ERROR] Client cleanup failed."
            << " Error: " << result << "\n";

        return 1;
    }

    std::cout
        << "[UR] Disconnected from Load Balancer.\n";

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
        << "[UR] Winsock resources released.\n"
        << "[UR] User stopped successfully.\n";

    return 0;
}