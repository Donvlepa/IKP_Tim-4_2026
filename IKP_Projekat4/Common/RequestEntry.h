#pragma once

#include <winsock2.h>
#include <cstdint>
#include "Enums.h"

struct RequestEntry
{
    uint64_t lbRequestId = 0;
    uint64_t clientRequestId = 0;

    Priority priority = Priority::LOW;
    RequestStatus status = RequestStatus::QUEUED;

    uint64_t attemptId = 0;

    bool responseSent = false;

    SOCKET userSocket = INVALID_SOCKET;
    bool userConnected = false;

    uint64_t assignedWorkerId = 0;

    // Constructors and destructor.
    RequestEntry() = default;
    RequestEntry(const RequestEntry& other);
    ~RequestEntry();

    // Copy assignment operator.
    RequestEntry& operator=(const RequestEntry& other);

    // Payload management.
    bool setData(const uint8_t* source, uint32_t size);
    const uint8_t* getData() const;
    uint32_t getDataSize() const;
    void clearData();

private:
    uint8_t* data = nullptr;
    uint32_t dataSize = 0;
};