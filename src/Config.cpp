#include "Config.hpp"
#include <string>
#include <stdexcept>

void Config::initCliApp(CLI::App& cliApp)
{
    static const std::map<std::string, LogLevel> logMap = {
        {"f", LogLevel::fatal}, {"e", LogLevel::error}, {"w", LogLevel::warn},
        {"i", LogLevel::info},  {"d", LogLevel::debug}, {"t", LogLevel::trace}
    };
    auto runMode = cliApp.add_option_group("RunMode", "Select implementation mode");

    runMode->add_option("-s", [this](CLI::results_t v) {
        return parseCIDR(v[0]);
    }, "Server mode: allocate IPv4 [Default: 192.168.208.0/24]");

    runMode->add_option("-c", [this](CLI::results_t v) {
        return parseRemoteAddr(v[0]);
    }, "Client mode: connect to (e.g., 1.2.3.4:3460)");

    runMode->require_option(0, 1);

    cliApp.add_option("-p,--port", localPort_, "Local listen port [Default: 3460]")
    ->check(CLI::Range(0, 65535));

    cliApp.add_option("-m", swPortIntvl_, "Source port switching interval in minutes")
    ->check(CLI::PositiveNumber);

    cliApp.add_option("--log", logLevel_, "Set log level [Default: INFO]")
    ->transform(CLI::CheckedTransformer(logMap, CLI::ignore_case))
    ->option_text("{f, e, w, i, d, t}");

    cliApp.add_flag("--nosync", noSync_, "Run without sync server");

    cliApp.add_flag("--aio", isAioEnable_, "Enable Async I/O (io_uring 6.1+ / IOCP)");

    cliApp.add_option("--passwd", passwd_, "Set password for encryption")
    ->each([this](const std::string&) {
        enableSec_ = true;
    })
    ->check([](const std::string &str) {
        if (str.empty()) {
            return std::string("Password cannot be empty");
        }
        if (str.length() > 16) {
            return std::string("Password length exceeds 16 characters");
        }
        return std::string("");
    });
}

bool Config::parseCIDR(const std::string& input)
{
    runMode_ = RunMode::server;
    std::string cidr = input;
    size_t idx = cidr.rfind('/');
    if (idx == std::string::npos) {
        return false;
    }

    std::string netNumLenStr = cidr.substr(idx + 1);
    try {
        netNumLen_ = std::stoi(netNumLenStr);
    } catch (...) {
        return false;
    }
    if (netNumLen_ < 0 || netNumLen_ > 32) {
        return false;
    }

    uint32_t netMask = (netNumLen_ == 0) ? 0 : (0xFFFFFFFF << (32 - netNumLen_));
    std::string netNumStr = cidr.substr(0, idx);
    if (inet_pton(AF_INET, netNumStr.c_str(), &netNum_) == 0) {
        return false;
    }
    netNum_ = ntohl(netNum_);
    netNum_ &= netMask;

    return true;
}

bool Config::parseRemoteAddr(const std::string& input) {
    runMode_ = RunMode::client;
    std::string hostPortPair = input;

    size_t idx = hostPortPair.rfind(':');
    if (idx == std::string::npos) {
        return false;
    }

    std::string portStr = hostPortPair.substr(idx + 1);
    try {
        int port = std::stoi(portStr);
        if (port < 0 || port > 65535) return false;
        remotePort_ = htons(static_cast<uint16_t>(port));
    } catch (...) {
        return false;
    }

    std::string hostStr = hostPortPair.substr(0, idx);
    std::string ipv6Str;

    if (hostStr.find(':') != std::string::npos) {
        if (hostStr.front() == '[' && hostStr.back() == ']') {
            ipv6Str = hostStr.substr(1, hostStr.size() - 2);
        } else {
            ipv6Str = hostStr;
        }
    } else {
        ipv6Str = "::ffff:" + hostStr;
    }

    if (inet_pton(AF_INET6, ipv6Str.c_str(), &remoteAddr_) <= 0) {
        return false;
    }

    return true;
}

bool Config::parseParams(int argc, char* argv[])
{
    CLI::App cliApp{"A high-performance virtual LAN based on TAP devices.\n"
                    "Runs in Server mode by default.",
                    "TapLan"};

    initCliApp(cliApp);

    try {
        cliApp.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit(cliApp.exit(e));
    }

    return true;
}
