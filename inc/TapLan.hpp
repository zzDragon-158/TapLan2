#pragma once
#include <cstdint>
#include <ctime>
#include <pthread.h>
#include "LogMgr.hpp"
#include "NodeMgr.hpp"
#include "Socket.hpp"
#include "TapDev.hpp"

typedef enum {
    RunMode_None = 0,
    RunMode_Server,
    RunMode_Client,
}RunModeT;

typedef struct {
    RunModeT    runMode;
    uint16_t    localPort;
    uint32_t    netNum;
    uint8_t     netNumLen;
    in6_addr    remoteAddr;
    uint16_t    remotePort;
    bool        isMultiPortEnable;
} ConfigDataT;

class TapLan {
public:
    static ConfigDataT config_;

    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showErrorCount();

private:
    bool            runFlag_;
    sockaddr_in6    serverAddr_;
    Mac             mac_;
    UdpSocket*      udpSockPtr_;
    UdpSocket*      udpSockPtrArr_[4];
    TcpSocket*      tcpSockPtr_;
    const char      *recvThreadName_, *sendThreadName_, *syncThreadName_;
    std::thread     recvThread_, sendThread_, syncThread_;

    void initUdpSockPtr();
    void handleSockData(void* buf, size_t bufLen, sockaddr_in6& srcAddr);
    void recvSockData();
    void handleTapData(void* buf, size_t bufLen);
    void readTapData();
    void syncNodeStatusToClients();
    void syncNodeStatusFromServer();
    void syncNodeStatus();
};
