#pragma     once
#include    <cstdint>
#include    <ctime>
#include    <pthread.h>
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "BsdSock.hpp"
#include    "TapDev.hpp"
#include    "UioIntf.hpp"

class TapLan {
public:
    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showStats();
    UdpSock* getUdpSockPtr();

    void handleUdpData(UioCtx* ctx);
    void handleTapData(UioCtx* ctx);

private:
    const char*     TAG = "[TapLan]";
    sockaddr_in6    serverAddr_;
    UdpSock*        udpSockPtr_;
    UdpSock*        udpSockPtrs_[4];
    std::shared_ptr<NodeMgr>    nodeMgrPtr_;
    UioIntf*         ioIntfPtr_;
    std::thread     syncThread_, aioWrkThread_, tapWrkThread_, udpWrkThread_;

    bool initUdpSockPtrs();
    void syncWrk();

    friend void SioIntf::udpWrk();
    friend void SioIntf::tapWrk();
    friend void AioIntf::aioWrk();
};
