#pragma once
#include <cstdint>
#include <ctime>
#include <pthread.h>
#include "LogMgr.hpp"
#include "NodeMgr.hpp"
#include "Socket.hpp"
#include "TapDev.hpp"

enum RunModeT{
    RunMode_None = 0,
    RunMode_Server,
    RunMode_Client,
};

struct ConfigDataT {
    uint8_t     runMode;
    uint16_t    localPort;
    uint32_t    netNum;
    uint8_t     netNumLen;
    in6_addr    remoteAddr;
    uint16_t    remotePort;
    bool        isMultiPortEnable;
    bool        isRunning;
    Mac         mac;

    ConfigDataT(): runMode(RunMode_Server), localPort(3460),
                    netNum((192 << 24) + (168 << 16) + (208 << 8)),
                    netNumLen(24), remoteAddr{}, remotePort(0),
                    isMultiPortEnable(false), isRunning(false),
                    mac{} {
        // nothing to do
    }
};

class TapLan {
public:
    static ConfigDataT config_;

    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showStats();

private:
    sockaddr_in6    serverAddr_;
    UdpSocket*      udpSockPtr_;
    UdpSocket*      udpSockPtrArr_[4];
    std::shared_ptr<NodeMgr>    nodeMgrPtr_;
    const char      *recvThreadName_, *sendThreadName_, *syncThreadName_;
    std::thread     recvThread_, sendThread_, syncThread_;

    void initUdpSockPtr();
    void handleSockData(uint8_t* buf, size_t bufLen, sockaddr_in6& srcAddr);
    void recvSockData();
    void handleTapData(uint8_t* buf, size_t bufLen);
    void readTapData();
    void syncNodeStatus();
};
