#pragma     once
#include    <cstdint>
#include    <ctime>
#include    <atomic>
#include    <pthread.h>
#include    <map>
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "BsdSock.hpp"
#include    "TapDev.hpp"
#include    "UioIntf.hpp"
#include    "Config.hpp"
#include    "DataSec.hpp"

class TapLan {
public:
    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showStats();

    void handleUdpData(UioCtx* ctx);
    void handleTapData(UioCtx* ctx);

private:
    struct EthHdr {
        Mac dst;
        Mac src;
        uint16_t type;
    };

    static constexpr char TAG[] = "[TapLan]";
    sockaddr_in6    serverAddr_;
    UdpSock*        udpSockPtr_;
    UdpSock*        udpSockPtrs_[4];
    std::atomic<UdpSock*>       udpSendSockPtr_;
    std::shared_ptr<NodeMgr>    nodeMgrPtr_;
    UioIntf*        uioIntfPtr_;
    AeadSessSPtr    sendSession_;
    std::map<uint64_t, AeadSessSPtr> macToSession_;
    std::thread     swPortThread_, syncThread_, aioWrkThread_, tapWrkThread_, udpWrkThread_;

    bool initUdpSockPtrs();
    void syncWrk();
    void swPortWrk();

    void unicastData(UioCtx* ctx);
    void broadcastData(UioCtx* ctx);

    void fetchMacFromNonce(const Nonce& nonce, Mac& mac);
    bool encryptData(UioCtx* ctx);
    bool decryptData(UioCtx* ctx);

    friend void SioIntf::udpWrk();
    friend void SioIntf::tapWrk();
    friend int AioIntf::initAioIntf();
    friend void AioIntf::aioWrk();
};
