#include <iostream>
#include "TestSuites.h"

int main()
{
    std::cout << "================================\n";
    std::cout << "       IKP PROJECT 4 TESTS       \n";
    std::cout << "================================\n\n";

    std::cout << "[ENUMS]\n";
    runEnumsTests();

    std::cout << "\n[CONSTANTS]\n";
    runConstantsTests();

    std::cout << "\n[REQUEST]\n";
    runRequestTests();

    std::cout << "\n[REQUEST ENTRY]\n";
    runRequestEntryTests();

    std::cout << "\n[WORKER ENTRY]\n";
    runWorkerEntryTests();

    std::cout << "\n[MESSAGE HEADER]\n";
    runMessageHeaderTests();

    std::cout << "\n[QUEUE]\n";
    runQueueTests();

    std::cout << "\n[HASH MAP]\n";
    runHashMapTests();

    std::cout << "\n[PROTOCOL]\n";
    runProtocolTests();

    std::cout << "\n[PAYLOADS]\n";
    runPayloadTests();

    std::cout << "\n[PAYLOAD CODEC]\n";
    runPayloadCodecTests();

    std::cout << "\n[MESSAGE BUFFER]\n";
    runMessageBufferTests();

    std::cout << "\n[WINSOCK CONTEXT]\n";
    runWinsockContextTests();

    std::cout << "\n[SOCKET UTILS]\n";
    runSocketUtilsTests();

    std::cout << "\n[SEND UTILS]\n";
    runSendUtilsTests();

    std::cout << "\n================================\n";
    std::cout << "All tests passed successfully.\n";
    std::cout << "================================\n";

    return 0;
}