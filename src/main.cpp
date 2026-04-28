#include <string>
#include "TapLan.hpp"

int main(int argc, char* argv[])
{
    g_cfgData.parseParams(argc, argv);

    LogMgrPtr->setLogLevel(g_cfgData.logLevel_);
    if (!LogMgrPtr->run())
        delayExit(-1, 3);

    TapLan* TapLanPtr = new TapLan();
    if (!TapLanPtr->run())
        delayExit(-1, 3);

    std::string input;
    LOGR(R"(enter /quit to exit)" "\n");
    while (true) {
        LOGR("TapLan> ");
        std::getline(std::cin, input);
        if (input == "/quit") {
            LOGR("Waiting for thread termination......\n");
            TapLanPtr->stop();
            break;
        } else if (input == "/show stats") {
            // TODO: flow stats
            // TapLanPtr->showStats();
        } else if (input == "/show fib") {
            TapLanPtr->showNodeStatus();
        }
    }

    delayExit(0, 3);
}
