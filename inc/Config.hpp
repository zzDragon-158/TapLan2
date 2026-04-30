#pragma     once
#include    <atomic>
#include    <getopt.h>
#include    "Common.hpp"
#include    "CLI11.hpp"

enum class RunMode: char {
    server = 0,
    client,
};

class Config {
public:
    static Config& instance() {
        static Config ins;
        return ins;
    };

    bool parseParams(int argc, char* argv[]);

    RunMode runMode() const noexcept { return runMode_; }
    uint16_t localPort() const noexcept { return localPort_; }
    uint32_t netNum() const noexcept { return netNum_; }
    uint8_t netNumLen() const noexcept { return netNumLen_; }
    const in6_addr& remoteAddr() const noexcept { return remoteAddr_; }
    uint16_t remotePort() const noexcept { return remotePort_; }
    uint16_t swPortIntvl() const noexcept { return swPortIntvl_; }
    LogLevel logLevel() const noexcept { return logLevel_; }
    bool enableSec() const noexcept { return enableSec_; }
    const std::string& passwd() const noexcept { return passwd_; }
    bool isAioEnable() const noexcept { return isAioEnable_; }
    bool noSync() const noexcept { return noSync_; }
    bool& running() noexcept { return running_; }

private:
    RunMode     runMode_ = RunMode::server;
    uint16_t    localPort_ = 3460;
    uint32_t    netNum_ = (192 << 24) + (168 << 16) + (208 << 8);
    uint8_t     netNumLen_ = 24;
    in6_addr    remoteAddr_;
    uint16_t    remotePort_;
    uint16_t    swPortIntvl_ = 0;
    LogLevel    logLevel_ = LogLevel::info;
    bool        enableSec_ = false;
    std::string passwd_ = "TapLan";
    bool        isAioEnable_ = false;
    bool        noSync_ = false;
    bool        running_ = false;

    void initCliApp(CLI::App& cliApp);
    bool parseCIDR(const std::string& input);
    bool parseRemoteAddr(const std::string& input);
};

inline Config& g_cfgData = Config::instance();
