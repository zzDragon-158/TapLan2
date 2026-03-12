#include <string>
#include <stdexcept>
#include <getopt.h>
#include "TapLan.hpp"

ConfigDataT g_cfgData;

TapLan* TapLanPtr = nullptr;

void parseParams(int argc, char* argv[]);
void printHelpInfo(const char* name);
void delayExit(int code, int64_t delaySeconds = 0);

int main(int argc, char* argv[])
{
    LogMgrPtr->run();
    while (!LogMgrPtr->isRunning());
    parseParams(argc, argv);

    TapLanPtr = new TapLan();
    if (!TapLanPtr->run())
        delayExit(-1, 3);

    std::string input;
    LOGR("enter \"/quit\" to exit\n");
    while (true) {
        LOGR("TapLan> ");
        std::getline(std::cin, input);
        if (input == "/quit") {
            LOGR("Waiting for thread termination......\n");
            TapLanPtr->stop();
            break;
        } else if (input == "/show stats") {
            TapLanPtr->showStats();
        } else if (input == "/show fib") {
            TapLanPtr->showNodeStatus();
        }
    }

    delayExit(0, 3);
}

void parseParams(int argc, char* argv[])
{
    static option longOpts[] = {
        {"help",        no_argument,            0,      'h'},
        {"ll",          required_argument,      0,      256},
        {"nosync",      no_argument,            0,      257},
        {"aio",         no_argument,            0,      258},
        {0, 0, 0, 0}
    };
    int opt;
    int optIdx = 0;

    while ((opt = getopt_long(argc, argv, "s:c:p:m:h", longOpts, &optIdx)) != -1) {
        switch (opt) {
            case 's': {
                g_cfgData.runMode = RunMode_Server;
                std::string cidr = optarg;
                size_t idx = cidr.rfind('/');
                if (idx == std::string::npos) {
                    LOGR("your input CIDR format[%s] is incorrect\n", cidr.c_str());
                    delayExit(-1);
                }

                std::string netNumLenStr = cidr.substr(idx + 1);
                try {
                    g_cfgData.netNumLen = std::stoi(netNumLenStr);
                } catch (const std::exception& e) {
                    LOGR("parse [%s] to network number failed, reason: %s\n", netNumLenStr.c_str(), e.what());
                    delayExit(-1);
                }

                std::string netNumStr = cidr.substr(0, idx);
                if (inet_pton(AF_INET, netNumStr.c_str(), &g_cfgData.netNum) == 0) {
                    LOGR("parse [%s] to network number failed\n", netNumStr.c_str());
                    delayExit(-1);
                }
                g_cfgData.netNum &= (1 << g_cfgData.netNumLen) - 1;
                g_cfgData.netNum = ntohl(g_cfgData.netNum);

                break;
            }
            case 'c': {
                g_cfgData.runMode = RunMode_Client;
                std::string hostPortPair = optarg;
                size_t idx = hostPortPair.rfind(':');
                if (idx == std::string::npos) {
                    LOGR("your input Host:Port format[%s] is incorrect\n", hostPortPair.c_str());
                    delayExit(-1);
                }

                std::string portStr = hostPortPair.substr(idx + 1);
                try {
                    g_cfgData.remotePort = htons(std::stoi(portStr));
                } catch (const std::exception& e) {
                    LOGR("parse [%s] to remote port failed, reason: %s\n", portStr.c_str(), e.what());
                    delayExit(-1);
                }

                std::string hostStr = hostPortPair.substr(0, idx);
                std::string ipv6Str;
                if (hostStr.rfind(':') != std::string::npos) {  // ipv6 addr
                    if (hostStr.front() != '[' || hostStr.back() != ']') {
                        ipv6Str = hostStr;
                    } else {
                        ipv6Str = hostStr.substr(1, idx - 2);
                    }
                } else {    // ipv4 addr
                    ipv6Str = std::string("::ffff:");
                    ipv6Str += hostStr;
                }
                if (inet_pton(AF_INET6, ipv6Str.c_str(), &(g_cfgData.remoteAddr)) == 0) {
                    LOGR("parse [%s] to remote ip address is invalid\n", ipv6Str.c_str());
                    delayExit(-1);
                }

                break;
            }
            case 'p': {
                try {
                    g_cfgData.localPort = std::stoi(optarg);
                    if (g_cfgData.localPort > 65535) {
                        LOGR("your input port number[%s] is invalid, range 0-65535\n", optarg);
                        delayExit(-1);
                    }
                } catch (const std::exception& e) {
                    LOGR("parse [%s] to local port failed, reason: %s\n", optarg, e.what());
                    delayExit(-1);
                }

                break;
            }
            case 'm':
                try {
                    g_cfgData.swPortIntvl = std::stoi(optarg);
                } catch (const std::exception& e) {
                    LOGR("parse [%s] to switch-port-interval failed, reason: %s\n", optarg, e.what());
                    delayExit(-1);
                }
                break;
            case 256: {
                LogLevel level = LOG_INFO;
                switch (optarg[0])
                {
                case 'f':
                case 'F':
                    level = LOG_FATAL;
                    break;

                case 'e':
                case 'E':
                    level = LOG_ERROR;
                    break;

                case 'w':
                case 'W':
                    level = LOG_WARN;
                    break;

                case 'i':
                case 'I':
                    level = LOG_INFO;
                    break;

                case 'd':
                case 'D':
                    level = LOG_DEBUG;
                    break;

                case 't':
                case 'T':
                    level = LOG_TRACE;
                    break;

                default:
                    LOGR("log level must a char in [fewidtFEWIDT].\n");
                    break;
                }
                LogMgrPtr->setLogLevel(level);
            }   break;
            case 257:
                g_cfgData.noSync = true;
                break;
            case 258:
                g_cfgData.isAioEnable = true;
                break;
            case '?':
            case 'h': {
                printHelpInfo(argv[0]);
                delayExit(0);
                
                break;
            }
        }
    }
}

void printHelpInfo(const char* name)
{
    LOGR("Usage as server: %s [-s <CIDR>] [-p <server port>]\n", name);
    LOGR("Usage as client: %s -c <host:port>\n", name);
    LOGR("Server or Client:\n");
    LOGR("  -p              <port>          local port\n");
    LOGR("  -m              <minutes>       cycle switching source ports with a switching interval of <minutes>\n");
    LOGR("  --ll            [fewidtFEWIDT]  set log level\n");
    LOGR("  --aio                           (IOCP/iouring) will be used, iouring requires Linux kernel version 6.1 or later\n");
    LOGR("  --nosync                        run without sync server\n");
    // TODO: support encrypt data
 // LOGR("  -k  <key>               use <key>(ASE-128) to encrypto data\n");
    LOGR("  -h,--help                       print the messages you see\n");
    LOGR("Server specific:\n");
    LOGR("  -s  <CIDR>                      run in server mode, allocate ipv4 address within <CIDR>(e.g. 192.168.208.0/24)\n");
    LOGR("Client specific:\n");
    LOGR("  -c  <host:port>                 run in client mode, connect to <host:port>(e.g. 192.168.208.1:3460, [::ffff:192.168.208.1]:3460)\n");
 // LOGR("  -d                      all data will be sent directly to the destination instead of the server\n");
}

void delayExit(int code, int64_t delaySeconds)
{
    LogMgrPtr->terminate();

    if (delaySeconds > 0) {
        std::cout << "Program will completely exit after "<< delaySeconds <<" seconds." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
    }

    exit(code);
}
