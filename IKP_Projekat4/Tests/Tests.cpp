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

    std::cout << "\n[MESSAGE IO]\n";
    runMessageIOTests();

    std::cout << "\n[NETWORK INTEGRATION]\n";
    runNetworkIntegrationTests();

    std::cout << "\n[TCP LISTENER]\n";
    runTcpListenerTests();

    std::cout << "\n[TCP CLIENT]\n";
    runTcpClientTests();

    std::cout << "\n[WORKER REGISTRATION]\n";
    runWorkerRegistrationTests();

    std::cout << "\n[REGISTRATION SERVER]\n";
    runRegistrationServerTests();

    std::cout << "\n[CONNECTION HANDOFF]\n";
    runConnectionHandoffTests();

    std::cout << "\n[TUR RECEIVER]\n";
    runTurReceiverTests();

    std::cout << "\n[TWR RECEIVER]\n";
    runTwrReceiverTests();

    std::cout << "\n[REQUEST REGISTRY]\n";
    runRequestRegistryTests();

    std::cout << "\n[TUR REQUEST INTEGRATION]\n";
    runTurRequestIntegrationTests();

    std::cout << "\n[WORKER REGISTRY]\n";
    runWorkerRegistryTests();

    std::cout << "\n[TWR WORKER INTEGRATION]\n";
    runTwrWorkerIntegrationTests();

    std::cout << "\n[RECEIVER SYSTEM INTEGRATION]\n";
    runReceiverSystemIntegrationTests();

    std::cout << "\n================================\n";
    std::cout << "All tests passed successfully.\n";
    std::cout << "================================\n";

    return 0;
}