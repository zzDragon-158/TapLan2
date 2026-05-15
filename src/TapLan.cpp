#include    "TapLan.hpp"
#include    <cstdint>
#include    <ctime>
#include    "BsdSock.hpp"
#include    "DataSec.hpp"
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "TapDev.hpp"
#include    "Config.hpp"
#include    "UioIntf.hpp"

std::string formatNum(uint64_t number)
{
    static const std::vector<std::string> units = {
        "",
        "M",
        "T"
    };
    constexpr int threshold = 1 << 20;

    if (number < threshold) {
        return std::format("{}", number);
    }

    int unitIdx = 0;
    while (number >= threshold && unitIdx < units.size() - 1) {
        number >>= 20;
        ++unitIdx;
    }

    return std::format("{}{}", number, units[unitIdx]);
}

TapLan::TapLan()
{
    g_cfgData.running() = (initUdpSockPtrs() && g_tapDev.isFdValid());
    nodeMgrPtr_ = std::make_shared<NodeMgr>();
    udpSendSockPtr_ = udpSockPtr_;
}

TapLan::~TapLan()
{
    stop();
}

bool TapLan::initUdpSockPtrs()
{
    uint16_t startPort = g_cfgData.localPort() - g_cfgData.localPort() % 4;
    for (int i = 0; i < 4; ++i) {
        UdpSock*& udpSockPtr = udpSockPtrs_[i];
        uint16_t port = startPort + i;
        if (!g_cfgData.swPortIntvl() && port != g_cfgData.localPort()) {
            continue;
        }

        udpSockPtr = new UdpSock(port);
        if (!udpSockPtr->isFdValid()) {
            delete udpSockPtr;
            udpSockPtr = nullptr;
            LOGW("Failed to bind [{}]port to udp socket.", port);
        }
        if (port == g_cfgData.localPort()) {
            udpSockPtr_ = udpSockPtr;
        }
    }

    return (udpSockPtr_ && udpSockPtr_->isFdValid());
}

void TapLan::syncWrk()
{
    if (g_cfgData.runMode() == RunMode::server) {
        nodeMgrPtr_->server();
    } else if (g_cfgData.runMode() == RunMode::client) {
        nodeMgrPtr_->client();
    } else {
        // RunMode_None
    }

    LOGI("syncWrk has exited.");
}

void TapLan::swPortWrk()
{
    while (g_cfgData.running()) {
        std::time_t now = std::time(nullptr);
        uint16_t portIdx = (now / 60 / g_cfgData.swPortIntvl()) % 4;
        for (int i = portIdx; i < portIdx + 4; ++i) {
            UdpSock* sockPtr = udpSockPtrs_[i % 4];
            if (!sockPtr) {
                continue;
            }

            udpSendSockPtr_ = sockPtr;
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(IO_WAIT_TIME));
    }

    LOGI("swPortWrk has exited.");
}

void TapLan::showNodeStatus()
{
    if (!nodeMgrPtr_) {
        LOGI("No other nodes have been obtained from the server.");
        return ;
    }

    LOGR("Status     TapLan MAC address    TapLan IP address    Public IP address\n");
//  LOGR("offline    00:00:00:00:00:00     255.255.255.255      aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb");

    nodeMgrPtr_->forEach([&](uint64_t m, NodeSessSPtr n) {
        NodeInfoSPtr nodeInfo = n->nodeInfo;
        std::string statusStr = (nodeInfo->status < NodeStatus::ipGot? "OFFLINE": "ONLINE");

        LOGR(
            "{:<11}{:<22}{:<21}[{}]:{}\n",
            statusStr,
            nodeInfo->mac,
            nodeInfo->ipv4Addr,
            nodeInfo->ipv6Addr,
            ntohs(nodeInfo->ipv6Port)
        );
    });
}

void TapLan::showStats()
{
    static constexpr char fColName[] = "    {:<4}{:<12}{:<12}{:<12}{:<12}\n";
    static constexpr char fColVal[] = "        {:<12}{:<12}{:<12}{:<12}\n";
    static std::string rxColName = std::format(
        fColName,
        "RX:",
        "bytes",
        "packets",
        "errors",
        "dropped"
    );
    static std::string txColName = std::format(
        fColName,
        "TX:",
        "bytes",
        "packets",
        "errors",
        "dropped"
    );

    auto printStat = [](const Stats& s) {
        LOGR(
            fColVal,
            formatNum(s.bytes),
            s.packets,
            s.errors,
            s.dropped
        );
    };

    LOGR("UDP:\n");
    LOGR(rxColName);
    printStat(udpRx_);
    LOGR(txColName);
    printStat(udpTx_);

    LOGR("TAP:\n");
    LOGR(rxColName);
    printStat(tapRx_);
    LOGR(txColName);
    printStat(tapTx_);
#if 0
    uint64_t totalSendBytes = 0, totalSendErrs = 0, totalRecvBytes = 0, totalRecvErrs = 0, totalDropped = 0;
    if (g_cfgData.swPortIntvl()) {
        LOGR("Each UDP:\n");
        for (int i = 0; i < 4; ++i) {
            uint64_t sendBytes = udpSockPtrs_[i]->getSendBytes(),
                     sendErrs = udpSockPtrs_[i]->getSendErrors(),
                     recvBytes = udpSockPtrs_[i]->getRecvBytes(),
                     recvErrs = udpSockPtrs_[i]->getRecvErrors(),
                     dropped = udpSockPtrs_[i]->getDropped();

            if (i == 3) {
                LOGR("╙── UDP port {}:\n", udpSockPtrs_[i]->getBindPort());
                LOGR("    ╟── TX bytes:   {}\n", sendBytes);
                LOGR("    ╟── TX errors:  {}\n", sendErrs);
                LOGR("    ╟── RX bytes:   {}\n", recvBytes);
                LOGR("    ╟── RX errors:  {}\n", recvErrs);
                LOGR("    ╙── dropped:    {}\n", dropped);
            } else {
                LOGR("╟── UDP port {}:\n", udpSockPtrs_[i]->getBindPort());
                LOGR("║   ╟── TX bytes:   {}\n", sendBytes);
                LOGR("║   ╟── TX errors:  {}\n", sendErrs);
                LOGR("║   ╟── RX bytes:   {}\n", recvBytes);
                LOGR("║   ╟── RX errors:  {}\n", recvErrs);
                LOGR("║   ╙── dropped:    {}\n", dropped);
            }
            totalSendBytes += sendBytes;
            totalSendErrs += sendErrs;
            totalRecvBytes += recvBytes;
            totalRecvErrs += recvErrs;
            totalDropped += dropped;
        }
    } else {
        totalSendBytes += udpSockPtr_->getSendBytes();
        totalSendErrs += udpSockPtr_->getSendErrors();
        totalRecvBytes += udpSockPtr_->getRecvBytes();
        totalRecvErrs += udpSockPtr_->getRecvErrors();
        totalDropped += udpSockPtr_->getDropped();
    }

    LOGR("\n");

    LOGR("Total UDP\n");
    LOGR("╟── TX bytes:       {}\n", totalSendBytes);
    LOGR("╟── TX errors:      {}\n", totalSendErrs);
    LOGR("╟── RX bytes:       {}\n", totalRecvBytes);
    LOGR("╟── RX errors:      {}\n", totalRecvErrs);
    LOGR("╙── dropped:        {}\n", totalDropped);

    LOGR("\n");

    LOGR("Total TAP:\n");
    LOGR("╟── write bytes:    {}\n", g_tapDev.getWriteBytes());
    LOGR("╟── write errors:   {}\n", g_tapDev.getWriteErrs());
    LOGR("╟── read  bytes:    {}\n", g_tapDev.getReadBytes());
    LOGR("╙── read  errors:   {}\n", g_tapDev.getReadErrs());
#endif
}

bool TapLan::run()
{
    if (!g_cfgData.running())
        return false;

    if (g_cfgData.swPortIntvl()) {
        swPortThread_ = std::thread(&TapLan::swPortWrk, this);
        pthread_setname_np(swPortThread_.native_handle(), "swPortWrk");
    }

    if (g_cfgData.isAioEnable()) {
        uioIntfPtr_ = new AioIntf(g_tapDev.getFd(), udpSockPtr_->getFd(), this);

        aioWrkThread_ = std::thread(&AioIntf::aioWrk, (AioIntf*)uioIntfPtr_);
        pthread_setname_np(aioWrkThread_.native_handle(), "aioWrk");
    } else {
        uioIntfPtr_ = new SioIntf(g_tapDev.getFd(), udpSockPtr_->getFd(), this);

        udpWrkThread_ = std::thread(&SioIntf::udpWrk, (SioIntf*)uioIntfPtr_);
        pthread_setname_np(udpWrkThread_.native_handle(), "udpWrk");

        tapWrkThread_ = std::thread(&SioIntf::tapWrk, (SioIntf*)uioIntfPtr_);
        pthread_setname_np(tapWrkThread_.native_handle(), "tapWrk");
    }

    if (!g_cfgData.noSync()) {
        syncThread_ = std::thread(&TapLan::syncWrk, this);
        pthread_setname_np(syncThread_.native_handle(), "syncWrk");
    }

    return true;
}

bool TapLan::stop()
{
    if (!g_cfgData.running())
        return false;

    g_cfgData.running() = false;
    g_tapDev.close();
    for (int i = 0; i < 4; ++i) {
        if (udpSockPtrs_[i])
            udpSockPtrs_[i]->close();
    }

    if (udpWrkThread_.joinable()) {
        udpWrkThread_.join();
    }

    if (tapWrkThread_.joinable()) {
        tapWrkThread_.join();
    }

    if (aioWrkThread_.joinable()) {
        aioWrkThread_.join();
    }

    if (syncThread_.joinable()) {
        syncThread_.join();
    }

    return true;
}

void TapLan::handleUdpData(UioCtx* ctx)
{
    if (!decryptData(ctx))
        return ;

    char* payload = ctx->buf->payload;
    EthHdr eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& dstMac = eh.dst;
    Mac& srcMac = eh.src;
    bool needBroadcast = eh.dst[0] & 0x01;

    if (g_cfgData.noSync()) {
        // sockaddr_in6& srcAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
        nodeMgrPtr_->addEmptyNode(srcMac);
    }

    if (!needBroadcast) {
        if (!unicastData(ctx)) {
            ++tapRx_.dropped;
        }
    } else {
        broadcastData(ctx);
    }
}

void TapLan::handleTapData(UioCtx* ctx)
{
    char* payload = ctx->buf->payload;
    EthHdr eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& dstMac = eh.dst;
    bool needBroadcast = eh.dst[0] & 0x01;

    if (!needBroadcast) {
        if (!unicastData(ctx)) {
            ++tapRx_.dropped;
        }
    } else {
        broadcastData(ctx);
    }
}

bool TapLan::unicastData(UioCtx* ctx)
{
    EthHdr eh = reinterpret_cast<EthHdr&>(*ctx->buf->payload);
    Mac& dstMac = eh.dst;
    if (dstMac == g_tapDev.getMac()) {
        uioIntfPtr_->univTapWrite(g_tapDev.getFd(), ctx);
        return true;
    }

    auto node = nodeMgrPtr_->findNode(dstMac);
    if (node) {
        sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
        NodeMgr::setSockaddr(dstAddr, node->nodeInfo);

        if (encryptData(ctx, node)) {
            SockFd udpSendFd = udpSendSockPtr_.load()->getFd();
            uioIntfPtr_->univUdpSend(udpSendFd, ctx);
            return true;
        } else {
            LOGE("Failed to encrypt.");
        }
    }

    return false;
}

void TapLan::broadcastData(UioCtx* ctx)
{
    SockFd udpSendFd = udpSendSockPtr_.load()->getFd();
    char* payload = ctx->buf->payload;
    EthHdr eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& srcMac = eh.src;

    if (g_cfgData.runMode() == RunMode::client) {
        if (srcMac != g_tapDev.getMac()) {
            uioIntfPtr_->univTapWrite(g_tapDev.getFd(), ctx);
            return ;
        }
        if (encryptData(ctx, nullptr)) {
            ctx->buf->addr = g_cfgData.serverAddr();
            uioIntfPtr_->univUdpSend(udpSendFd, ctx);
        }

        return ;
    }

    nodeMgrPtr_->forEach([&](uint64_t m, NodeSessSPtr n) {
        NodeInfoSPtr nodeInfo = n->nodeInfo;
        if (nodeInfo->status < NodeStatus::ipGot || nodeInfo->mac == srcMac) {
            return ;
        }

        if (nodeInfo->mac == g_tapDev.getMac() && srcMac != g_tapDev.getMac()) {
            uioIntfPtr_->univTapWrite(g_tapDev.getFd(), ctx);
            return ;
        }

        UioCtx* sendCtx = nullptr;
        if (g_cfgData.isAioEnable()) {
            sendCtx = uioIntfPtr_->acquireIoCtx();
            if (sendCtx == nullptr) return ;
            memcpy(sendCtx->buf->payload, payload, ctx->dataLen);
            sendCtx->dataLen = ctx->dataLen;
        } else {
            sendCtx = ctx;
        }
        sockaddr_in6& sendAddr = reinterpret_cast<sockaddr_in6&>(sendCtx->buf->addr);
        NodeMgr::setSockaddr(sendAddr, nodeInfo);

        if (encryptData(sendCtx, n)) {
            uioIntfPtr_->univUdpSend(udpSendFd, sendCtx);
        }
    });
}

bool TapLan::encryptData(UioCtx* ctx, NodeSessSPtr node)
{
    if (!g_cfgData.enableSec()) {
        return true;
    }

    AeadSessSPtr session = node? node->aeadSess: nodeMgrPtr_->getAeadSess();
    if (!session) {
        return false;
    }
 
    AeadPacket* packet = reinterpret_cast<AeadPacket*>(&ctx->buf->nonce);
    session->encrypt(packet, ctx->dataLen);

    return true;
}

bool TapLan::decryptData(UioCtx* ctx)
{
    if (!g_cfgData.enableSec()) {
        return true;
    }

    if (ctx->dataLen < NONCE_SIZE) {
        return false;
    }

    Nonce& recvNonce = ctx->buf->nonce;
    Mac srcMac = AeadSession::fetchMacFromNonce(recvNonce);
    NodeSessSPtr node = nodeMgrPtr_->findNode(srcMac);
    AeadSessSPtr session = node? node->aeadSess: nodeMgrPtr_->getAeadSess();
    if (!session) {
        return false;
    }

    ctx->dataLen -= NONCE_SIZE;
    AeadPacket* packet = reinterpret_cast<AeadPacket*>(&recvNonce);
    if (!session->decrypt(packet, ctx->dataLen)) {
        LOGE("Failed to decrypt packet from [{}:{}]",
            static_cast<uint64_t>(srcMac), ctx->buf->addr.sin6_addr);
        return false;
    }

    return true;
}
