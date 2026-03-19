#pragma     once
#include    <cstdint>
#include    <ctime>
#include    <pthread.h>
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "BsdSock.hpp"
#include    "TapDev.hpp"
#include    "AioIntf.hpp"
#include    "SioIntf.hpp"

class TapLan {
public:
    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showStats();
    UdpSocket* getUdpSockPtr();

private:
    sockaddr_in6    serverAddr_;
    UdpSocket*      udpSockPtr_;
    UdpSocket*      udpSockPtrs_[4];
    std::shared_ptr<NodeMgr>    nodeMgrPtr_;
    std::thread     syncThread_, aioWrkThread_, sioWrkThread_, tapWrkThread_, udpWrkThread_;

    bool initUdpSockPtrs();
    void syncWrk();

    void handleUdpData(SioIntf& sioIntf, SioIntf::Ctx* ctx);
    void handleTapData(SioIntf& sioIntf, SioIntf::Ctx* ctx);
    void udpWrk();
    void tapWrk();
    void sioWrk();

    void handleTapRead(AioIntf::Ctx* ctx);
    void handleTapWrite(AioIntf::Ctx* ctx);
    void handleUdpRecv(AioIntf::Ctx* ctx);
    void handleUdpSend(AioIntf::Ctx* ctx);
    void aioWrk();
};
