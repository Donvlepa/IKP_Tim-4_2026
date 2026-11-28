#pragma once

// Manages the lifetime of the Winsock library.
class WinsockContext
{
public:
    WinsockContext() noexcept;

    ~WinsockContext() noexcept;

    // Prevents multiple objects from owning the same initialization.
    WinsockContext(const WinsockContext&) = delete;

    WinsockContext& operator=(
        const WinsockContext&
        ) = delete;

    WinsockContext(WinsockContext&&) = delete;

    WinsockContext& operator=(
        WinsockContext&&
        ) = delete;

    // Initializes Winsock version 2.2.
    // Returns zero on success or a Winsock error code.
    int initialize() noexcept;

    // Releases the acquired Winsock resources.
    // Returns zero on success or a Winsock error code.
    int cleanup() noexcept;

    // Checks whether Winsock is initialized by this object.
    bool isInitialized() const noexcept;

private:
    bool startupAcquired_;
    bool initialized_;
};