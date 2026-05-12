#include <string>
#include <future>
#include <chrono>
#include "Common.hpp"
#include "Config.hpp"
#include "TapLan.hpp"

int main(int argc, char* argv[])
{
    g_cfgData.parseParams(argc, argv);

    g_logMgr.setLogLevel(g_cfgData.logLevel());
    if (!g_logMgr.run())
        delayExit(-1, 3);

    TapLan* TapLanPtr = new TapLan();
    if (!TapLanPtr->run())
        delayExit(-1, 3);

    auto getInput = []() {
        LOGR("TapLan> ");
        std::string s;
        if (std::getline(std::cin, s))
            return s;
        return std::string("");
    };
    std::future<std::__async_result_of<decltype(getInput)>> futureInput;
    futureInput = std::async(std::launch::async, getInput);
    while (g_cfgData.running()) {
        if (futureInput.wait_for(std::chrono::seconds(IO_WAIT_TIME)) != std::future_status::ready) {
            continue;
        }

        std::string input = futureInput.get();
        if (input == "/quit") {
            break;
        } else if (input == "/show stats") {
            // TODO: flow stats
            // TapLanPtr->showStats();
        } else if (input == "/show fib") {
            TapLanPtr->showNodeStatus();
        }

        futureInput = std::async(std::launch::async, getInput);
    }

    LOGR("Waiting for thread termination......\n");
    TapLanPtr->stop();
    delayExit(0, 3);
}
