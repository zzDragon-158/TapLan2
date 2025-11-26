#include <string>
#include <stdexcept>
#include <getopt.h>
#include "TapLan.hpp"

#define cfgData TapLan::config_

TapLan* TapLanPtr = nullptr;

void setDefaultConfig();
void parseParams(int argc, char* argv[]);
void printHelpInfo(const char* name);
void delayExit(int code, int64_t delaySeconds = 0);

int main(int argc, char* argv[])
{
    LogMgrPtr->run();
    while (!LogMgrPtr->isRunning());
    setDefaultConfig();
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
        } else if (input == "/show err") {
            TapLanPtr->showErrorCount();
        } else if (input == "/show fib") {
            TapLanPtr->showNodeStatus();
        }
    }

    delayExit(0, 3);
}

void setDefaultConfig()
{
    cfgData.runMode = RunMode_Server;
    cfgData.localPort = 3460;
    cfgData.netNum = (192 << 24) + (168 << 16) + (208 << 8);
    cfgData.netNumLen = 24;
    memset(&cfgData.remoteAddr, 0, sizeof(in6_addr));
    cfgData.remotePort = 3460;
    cfgData.isMultiPortEnable = false;
}

void parseParams(int argc, char* argv[])
{
    int opt;
    char ipv6Str[64] = "::ffff:";
    const size_t ipv6StrOffset = 7;

    while ((opt = getopt(argc, argv, "s:c:p:mh")) != -1) {
        switch (opt) {
            case 's': {
                cfgData.runMode = RunMode_Server;
                std::string cidr = optarg;
                size_t idx = cidr.rfind('/');
                if (idx == std::string::npos) {
                    LOGR("your input cidr is invalid\n");
                    delayExit(-1);
                }

                std::string netNumLenStr = cidr.substr(idx + 1);
                try {
                    cfgData.netNumLen = std::stoi(netNumLenStr);
                } catch (const std::exception& e) {
                    LOGR("parse [%s] to network number failed, reason: %s.\n", netNumLenStr.c_str(), e.what());
                    delayExit(-1);
                }

                std::string netNumStr = cidr.substr(0, idx);
                if (inet_pton(AF_INET, netNumStr.c_str(), &cfgData.netNum) == 0) {
                    LOGR("parse [%s] to network number failed.\n", netNumStr.c_str());
                    delayExit(-1);
                }
                cfgData.netNum &= (1 << cfgData.netNumLen) - 1;
                cfgData.netNum = ntohl(cfgData.netNum);

                break;
            }
            case 'c': {
                // TODO: Support to parse <Ipv4/v6>:<port>
                size_t optargLen = strlen(optarg);
                if (optargLen > 39) {
                    LOGR("your input IP address length exceeds the limit\n");
                    delayExit(-1, 3);
                }

                cfgData.runMode = RunMode_Client;
                strncpy(ipv6Str + ipv6StrOffset, optarg, optargLen);
                if (inet_pton(AF_INET6, ipv6Str, &(cfgData.remoteAddr)) == 0) {
                    if (inet_pton(AF_INET6, ipv6Str + ipv6StrOffset, &(cfgData.remoteAddr)) == 0) {
                        LOGR("your input IP address is invalid\n");
                        delayExit(-1, 0);
                    // } else {
                    //     memmove(ipv6Str, ipv6Str + ipv6StrOffset, optargLen);
                    //     ipv6Str[optargLen] = '\0';
                    }
                }

                break;
            }
            case 'p': {
                cfgData.localPort = atoi(optarg);
                if (cfgData.localPort > 65535) {
                    LOGR("port number is invalid, range 0-65535\n");
                    delayExit(-1, 0);
                }

                break;
            }
            case 'm': {
                cfgData.isMultiPortEnable = true;

                break;
            }
            case '?':
            case 'h': {
                printHelpInfo(argv[0]);
                delayExit(0, 0);
                
                break;
            }
        }
    }
}

void printHelpInfo(const char* name)
{
    LOGR("Usage as server: %s [-s <CIDR>] [-p <server port>] [-k <aes key>]\n", name);
    LOGR("Usage as client: %s [-c <server address>] [-p <server port>] [-k <aes key>]\n", name);
    LOGR("Server or Client:\n");
    LOGR("  -p  <port>              local port\n");
    LOGR("  -m                      enable multi-port transport mode\n");
    LOGR("  -k  <key>               use <key>(ASE-128) to encrypto data\n");
    LOGR("  -h                      print the messages you see\n");
    LOGR("Server specific:\n");
    LOGR("  -s  <CIDR>              run in server mode, allocate ipv4 address within <CIDR>\n");
    LOGR("Client specific:\n");
    LOGR("  -c  <Ipv4/v6>:<port>    run in client mode, connect to <Ipv4/v6>:<port>\n");
 // LOGR("  -d                      all data will be sent directly to the destination instead of the server\n");
}

void delayExit(int code, int64_t delaySeconds)
{
    LogMgrPtr->terminate();

    if (delaySeconds > 0) {
        std::cout << "Program will terminate after "<< delaySeconds <<" seconds." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
    }
    std::cout << "Program terminated with exit code " << code << std::endl;

    exit(code);
}
