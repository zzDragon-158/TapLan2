#include <getopt.h>
#include "TapLan.hpp"

TapLan* TapLanPtr = nullptr;
const char* TAG = "TapLan";
// default configuration
RunMode runMode = RunMode_Server;
char serverIpv6Addr[64] = "::ffff:";
const size_t ipv6AddrOffset = 7;
uint16_t serverPort = 3460;
uint32_t netId = (192 << 24) + (168 << 16) + (208 << 8);
uint8_t netIdLen = 24;

void delayExit(int code, int64_t delaySeconds)
{
    if (delaySeconds > 0) {
        std::cout << "The program will terminate after " << delaySeconds << " seconds." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
    }
    std::cout << "Program terminated with exit code " << code << std::endl;

    exit(code);
}

void printHelpInfo(const char* name)
{
    printf("Usage as server: %s [-s <CIDR>] [-p <server port>] [-k <aes key>]\n", name);
    printf("Usage as client: %s [-c <server address>] [-p <server port>] [-k <aes key>]\n", name);
    // printf("Server or Client:\n");
    printf("    -p      <server port>       server port to listen on/connect to <server port>\n");
    // printf("    -k      <key>               use <key>(ASE-128) to encrypto data\n");
    printf("Server specific:\n");
    // printf("    -s      <CIDR>              run in server mode, allocate ipv4 address within <CIDR>\n");
    printf("Client specific:\n");
    printf("    -c      <server address>    run in client mode, connect to <server address>");
    // printf("    -d                          all data will be sent directly to the destination instead of the server\n");
}

void parseParams(int argc, char* argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "c:p:h")) != -1) {
        if (opt == 'c') {
            size_t optargLen = strlen(optarg);
            if (optargLen > 39) {
                fprintf(stderr, "your input IP address is invalid\n");
                delayExit(-1, 3);
            }

            runMode = RunMode_Client;
            strncpy(serverIpv6Addr + ipv6AddrOffset, optarg, optargLen);
            in6_addr ipv6Addr;
            if (inet_pton(AF_INET6, serverIpv6Addr, &(ipv6Addr)) == 0) {
                if (inet_pton(AF_INET6, serverIpv6Addr + ipv6AddrOffset, &(ipv6Addr)) == 0) {
                    fprintf(stderr, "your input IP address is invalid\n");
                    delayExit(-1, 3);
                } else {
                    memmove(serverIpv6Addr, serverIpv6Addr + ipv6AddrOffset, optargLen);
                    serverIpv6Addr[optargLen] = '\0';
                }
            }
        } else if (opt == 'p') {
            serverPort = atoi(optarg);
            if (serverPort > 65535) {
                fprintf(stderr, "port number is invalid, range 0-65535");
                delayExit(-1, 3);
            }
        } else if (opt == 'h') {
            printHelpInfo(argv[0]);
            delayExit(0, 0);
        }
    }
}

int main(int argc, char* argv[])
{
    LOGI(TAG, "Hello, world!");
    parseParams(argc, argv);

    if (runMode == RunMode_Server) {
        std::cout << "We are running in server mode." << std::endl;
        TapLanPtr = new TapLan(serverPort);
    } else {
        std::cout << "We are running in client mode." << std::endl;
        TapLanPtr = new TapLan(serverIpv6Addr, serverPort);
    }
    if (!TapLanPtr->run())
        delayExit(-1, 3);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
