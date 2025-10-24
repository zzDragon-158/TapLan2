#pragma once
#include "LogMgr.hpp"
#include "NodeMgr.hpp"
#include "Socket.hpp"
#include "TapDev.hpp"

typedef enum {
    RunMode_None = 0,
    RunMode_Server,
    RunMode_Client,
}RunMode;

class TapLan {
public:
    TapLan() = delete;
    TapLan(uint16_t port);                                      // server
    TapLan(const char* ipv6Addr, const uint16_t port);          // client
    ~TapLan();
    bool run();
    bool stop();

private:
    bool            runFlag_;
    RunMode         runMode_;
    sockaddr_in6    serverAddr_;
    Mac             mac_;
    UdpSocket*      udpSockPtr_;
    TcpSocket*      tcpSockPtr_;
    std::thread     threadRecvSockData_, threadReadTapData_, threadSyncNodeStatus_;

    void handleSockData(void* buf, size_t bufLen, sockaddr_in6& srcAddr);
    void recvSockData();
    void handleTapData(void* buf, size_t bufLen);
    void readTapData();
    void syncNodeStatusToClient();
    void syncNodeStatusFromServer();
    void syncNodeStatus();
};
