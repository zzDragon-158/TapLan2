#pragma     once
#include    <cstdint>
#include    <atomic>
#include    <pthread.h>
#include    "NodeMgr.hpp"
#include    "BsdSock.hpp"
#include    "UioIntf.hpp"

struct Stats {
    uint64_t bytes = 0;
    uint64_t packets = 0;
    uint64_t errors = 0;
    uint64_t dropped = 0;
};

class TapLan {
public:
    Stats udpTx_;
    Stats udpRx_;
    Stats tapTx_;
    Stats tapRx_;

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
    UdpSock* udpSockPtr_ = nullptr;
    UdpSock* udpSockPtrs_[4] = {};
    std::atomic<UdpSock*> udpSendSockPtr_ = nullptr;
    std::shared_ptr<NodeMgr> nodeMgrPtr_ = nullptr;
    UioIntf* uioIntfPtr_ = nullptr;
    std::thread swPortThread_, syncThread_, aioWrkThread_, tapWrkThread_, udpWrkThread_;

    bool initUdpSockPtrs();
    void syncWrk();
    void swPortWrk();

    bool unicastData(UioCtx* ctx);
    void broadcastData(UioCtx* ctx);

    bool encryptData(UioCtx* ctx, NodeSessSPtr node);
    bool decryptData(UioCtx* ctx);

    friend void SioIntf::udpWrk();
    friend void SioIntf::tapWrk();
    friend int AioIntf::initAioIntf();
    friend void AioIntf::aioWrk();
};
