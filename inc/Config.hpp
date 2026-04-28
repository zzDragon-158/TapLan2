#pragma     once
#include    <getopt.h>
#include    "Common.hpp"
#include    "CLI11.hpp"

#define     g_cfgData       Config::instance()

enum class RunMode: char {
    server = 0,
    client,
};

class Config {
public:
    RunMode     runMode_;
    uint16_t    localPort_;
    uint32_t    netNum_;
    uint8_t     netNumLen_;
    in6_addr    remoteAddr_;
    uint16_t    remotePort_;
    uint16_t    swPortIntvl_;
    LogLevel    logLevel_;
    bool        isAioEnable_;
    bool        noSync_;
    bool        running_;
    Mac         mac_;

    static Config& instance() {
        static Config ins;
        return ins;
    };

    void initCliApp(CLI::App& cliApp);
    bool parseParams(int argc, char* argv[]);
    bool parseCIDR(const std::string& input);
    bool parseRemoteAddr(const std::string& input);

    Config();
};
