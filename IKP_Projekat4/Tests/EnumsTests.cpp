#include <iostream>
#include "Enums.h"
#include "TestSuites.h"

static_assert(sizeof(Priority) == 1,
    "Invalid Priority size");

static_assert(sizeof(RequestStatus) == 1,
    "Invalid RequestStatus size");

static_assert(sizeof(WorkerStatus) == 1,
    "Invalid WorkerStatus size");

static_assert(sizeof(MessageType) == 2,
    "Invalid MessageType size");

void runEnumsTests()
{
    std::cout << "All enum size checks passed.\n";
}