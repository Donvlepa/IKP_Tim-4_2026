#include "pch.h"

#include <winsock2.h>

#include "WinsockContext.h"

#pragma comment(lib, "Ws2_32.lib")

// Initializes the context without acquiring resources.
WinsockContext::WinsockContext() noexcept
    : startupAcquired_(false),
    initialized_(false)
{
}

// Releases Winsock resources when the object is destroyed.
WinsockContext::~WinsockContext() noexcept
{
    cleanup();
}

// Initializes Winsock version 2.2.
int WinsockContext::initialize() noexcept
{
    // Prevent duplicate initialization.
    if (initialized_)
    {
        return 0;
    }

    // A previous initialization could not be fully released.
    if (startupAcquired_)
    {
        return WSAVERNOTSUPPORTED;
    }

    WSADATA wsaData = {};

    const int result = WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    );

    // WSAStartup returns the error code directly.
    if (result != 0)
    {
        return result;
    }

    startupAcquired_ = true;

    // Verify the negotiated Winsock version.
    if (LOBYTE(wsaData.wVersion) != 2 ||
        HIBYTE(wsaData.wVersion) != 2)
    {
        const int cleanupResult = cleanup();

        if (cleanupResult != 0)
        {
            return cleanupResult;
        }

        return WSAVERNOTSUPPORTED;
    }

    initialized_ = true;

    return 0;
}

// Releases Winsock resources acquired by this object.
int WinsockContext::cleanup() noexcept
{
    // No resources have been acquired.
    if (!startupAcquired_)
    {
        return 0;
    }

    const int result = WSACleanup();

    if (result == SOCKET_ERROR)
    {
        return WSAGetLastError();
    }

    startupAcquired_ = false;
    initialized_ = false;

    return 0;
}

// Returns the current initialization state.
bool WinsockContext::isInitialized() const noexcept
{
    return initialized_;
}