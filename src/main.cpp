#include <string>
#include <stdexcept>
#include <getopt.h>
#include "TapLan.hpp"

#define cfgData TapLan::config_

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
    int opt;

    while ((opt = getopt(argc, argv, "s:c:p:muh")) != -1) {
        switch (opt) {
            case 's': {
                cfgData.runMode = RunMode_Server;
                std::string cidr = optarg;
                size_t idx = cidr.rfind('/');
                if (idx == std::string::npos) {
                    LOGR("your input CIDR format[%s] is incorrect\n", cidr.c_str());
                    delayExit(-1);
                }

                std::string netNumLenStr = cidr.substr(idx + 1);
                try {
                    cfgData.netNumLen = std::stoi(netNumLenStr);
                } catch (const std::exception& e) {
                    LOGR("parse [%s] to network number failed, reason: %s\n", netNumLenStr.c_str(), e.what());
                    delayExit(-1);
                }

                std::string netNumStr = cidr.substr(0, idx);
                if (inet_pton(AF_INET, netNumStr.c_str(), &cfgData.netNum) == 0) {
                    LOGR("parse [%s] to network number failed\n", netNumStr.c_str());
                    delayExit(-1);
                }
                cfgData.netNum &= (1 << cfgData.netNumLen) - 1;
                cfgData.netNum = ntohl(cfgData.netNum);

                break;
            }
            case 'c': {
                cfgData.runMode = RunMode_Client;
                std::string hostPortPair = optarg;
                size_t idx = hostPortPair.rfind(':');
                if (idx == std::string::npos) {
                    LOGR("your input Host:Port format[%s] is incorrect\n", hostPortPair.c_str());
                    delayExit(-1);
                }

                std::string portStr = hostPortPair.substr(idx + 1);
                try {
                    cfgData.remotePort = htons(std::stoi(portStr));
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
                if (inet_pton(AF_INET6, ipv6Str.c_str(), &(cfgData.remoteAddr)) == 0) {
                    LOGR("parse [%s] to remote ip address is invalid\n", ipv6Str.c_str());
                    delayExit(-1);
                }

                break;
            }
            case 'p': {
                try {
                    cfgData.localPort = std::stoi(optarg);
                    if (cfgData.localPort > 65535) {
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
                cfgData.isMultiPortEnable = true;
                break;
            case 'u':
                cfgData.isIoUringEnable = true;
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
    LOGR("Usage as server: %s [-s <CIDR>] [-p <server port>] [-k <aes key>]\n", name);
    LOGR("Usage as client: %s [-c <server address>] [-p <server port>] [-k <aes key>]\n", name);
    LOGR("Server or Client:\n");
    LOGR("  -p  <port>              local port\n");
    LOGR("  -m                      enable multi-port transport mode\n");
    // TODO: support encrypt data
    LOGR("  -k  <key>               use <key>(ASE-128) to encrypto data\n");
    LOGR("  -h                      print the messages you see\n");
    LOGR("Server specific:\n");
    LOGR("  -s  <CIDR>              run in server mode, allocate ipv4 address within <CIDR>(e.g. 192.168.208.0/24)\n");
    LOGR("Client specific:\n");
    LOGR("  -c  <Host:Port>         run in client mode, connect to <Host:Port>(e.g. 192.168.208.1:3460, [::ffff:192.168.208.1]:3460)\n");
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
