#include "pch.h"
#include "RequestEntry.h"
#include "Constants.h"
#include <cstring>

// Creates an independent copy of another entry.
RequestEntry::RequestEntry(const RequestEntry& other)
{
    *this = other;
}

// Releases dynamically allocated request data.
RequestEntry::~RequestEntry()
{
    clearData();
}

// Copies request information and payload.
RequestEntry& RequestEntry::operator=(const RequestEntry& other)
{
    if (this == &other)
    {
        return *this;
    }

    setData(other.data, other.dataSize);

    lbRequestId = other.lbRequestId;
    clientRequestId = other.clientRequestId;

    priority = other.priority;
    status = other.status;

    attemptId = other.attemptId;
    responseSent = other.responseSent;

    userSocket = other.userSocket;
    userConnected = other.userConnected;

    assignedWorkerId = other.assignedWorkerId;

    return *this;
}

// Allocates memory and copies request data.
bool RequestEntry::setData(const uint8_t* source,
    uint32_t size)
{
    if (size > Constants::MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    if (size > 0 && source == nullptr)
    {
        return false;
    }

    if (size == 0)
    {
        clearData();
        return true;
    }

    uint8_t* newData = new uint8_t[size];

    std::memcpy(newData, source, size);

    clearData();

    data = newData;
    dataSize = size;

    return true;
}

// Returns the request payload.
const uint8_t* RequestEntry::getData() const
{
    return data;
}

// Returns the payload size.
uint32_t RequestEntry::getDataSize() const
{
    return dataSize;
}

// Releases the payload memory.
void RequestEntry::clearData()
{
    delete[] data;

    data = nullptr;
    dataSize = 0;
}