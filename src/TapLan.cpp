#include    "TapLan.hpp"

TapLan::TapLan()
    : serverAddr_{}
    , udpSockPtr_(nullptr)
    , udpSockPtrs_{}
    , nodeMgrPtr_(nullptr)
{
    if (g_cfgData.runMode_ == RunMode::server) {
        LOGI(TAG, "We are running in server mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        serverAddr_.sin6_port = htons(g_cfgData.localPort_);

        g_cfgData.running_ = initUdpSockPtrs() && TapDevPtr->isFdValid();

        if (g_cfgData.running_) {
            TapDevPtr->getMacAddr(g_cfgData.mac_);
            NodeSPtr n = nodeMgrPtr_->addNode(&serverAddr_, g_cfgData.mac_);
            TapDevPtr->setIPv4Addr(&n->ipv4Addr, g_cfgData.netNumLen_);
        }
    } else if (g_cfgData.runMode_ == RunMode::client) {
        LOGI(TAG, "We are running in client mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        serverAddr_.sin6_addr = g_cfgData.remoteAddr_;
        serverAddr_.sin6_port = g_cfgData.remotePort_;

        g_cfgData.running_ = initUdpSockPtrs() && TapDevPtr->isFdValid();

        if (g_cfgData.running_) {
            TapDevPtr->getMacAddr(g_cfgData.mac_);
        }
    } else {
        // RunMode_None
    }
    udpSendSockPtr_ = udpSockPtr_;
}

TapLan::~TapLan()
{
    stop();
}

bool TapLan::initUdpSockPtrs()
{
    uint16_t startPort = g_cfgData.localPort_ - g_cfgData.localPort_ % 4;
    for (int i = 0; i < 4; ++i) {
        UdpSock*& udpSockPtr = udpSockPtrs_[i];
        uint16_t port = startPort + i;
        if (!g_cfgData.swPortIntvl_ && port != g_cfgData.localPort_) {
            continue;
        }

        udpSockPtr = new UdpSock(port);
        if (!udpSockPtr->isFdValid()) {
            delete udpSockPtr;
            udpSockPtr = nullptr;
            LOGW(TAG, "Failed to bind [%u]port to udp socket.", port);
        }
        if (port == g_cfgData.localPort_) {
            udpSockPtr_ = udpSockPtr;
        }
    }

    return (udpSockPtr_ && udpSockPtr_->isFdValid());
}

void TapLan::syncWrk()
{
    if (g_cfgData.runMode_ == RunMode::server) {
        nodeMgrPtr_->server();
    } else if (g_cfgData.runMode_ == RunMode::client) {
        nodeMgrPtr_->client();
    } else {
        // RunMode_None
    }

    LOGI(TAG, "syncWrk has exited.");
}

void TapLan::swPortWrk()
{
    while (g_cfgData.running_) {
        std::time_t now = std::time(nullptr);
        uint16_t portIdx = (now / 60 / g_cfgData.swPortIntvl_) % 4;
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

    LOGI(TAG, "swPortWrk has exited.");
}

void TapLan::showNodeStatus()
{
    if (!nodeMgrPtr_) {
        LOGI(TAG, "No other nodes have been obtained from the server.");
        return ;
    }

    LOGR("Status     TapLan MAC address    TapLan IP address    Public IP address\n");
//  LOGR("offline    00:00:00:00:00:00     255.255.255.255      aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb");

    nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
        char tapmacbuf[32];
        sprintf(tapmacbuf, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
            n->mac.addr[0], n->mac.addr[1], n->mac.addr[2], 
            n->mac.addr[3], n->mac.addr[4], n->mac.addr[5]);

        std::string ipv6str = IPv6_NTOP(n->ipv6Addr);

        char tapipbuf[INET_ADDRSTRLEN];
        uint8_t* ipv4addr = reinterpret_cast<uint8_t*>(&(n->ipv4Addr));
        sprintf(tapipbuf, "%u.%u.%u.%u", ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3]);

        char buf[128];
        sprintf(buf, "%-11s%-22s%-21s[%s]:%u\n",
            (n->status == NODE_ONLINE? "ONLINE": "OFFLINE"),
            tapmacbuf, tapipbuf, ipv6str.c_str(), ntohs(n->ipv6Port));
        LOGR("%s", buf);
    });
}

#if 0
void TapLan::showStats()
{
    uint64_t totalSendBytes = 0, totalSendErrs = 0, totalRecvBytes = 0, totalRecvErrs = 0, totalDropped = 0;
    if (g_cfgData.swPortIntvl_) {
        LOGR("Each UDP:\n");
        for (int i = 0; i < 4; ++i) {
            uint64_t sendBytes = udpSockPtrs_[i]->getSendBytes(),
                     sendErrs = udpSockPtrs_[i]->getSendErrors(),
                     recvBytes = udpSockPtrs_[i]->getRecvBytes(),
                     recvErrs = udpSockPtrs_[i]->getRecvErrors(),
                     dropped = udpSockPtrs_[i]->getDropped();

            if (i == 3) {
                LOGR("╙── UDP port %u:\n", udpSockPtrs_[i]->getBindPort());
                LOGR("    ╟── TX bytes:   %lu\n", sendBytes);
                LOGR("    ╟── TX errors:  %lu\n", sendErrs);
                LOGR("    ╟── RX bytes:   %lu\n", recvBytes);
                LOGR("    ╟── RX errors:  %lu\n", recvErrs);
                LOGR("    ╙── dropped:    %lu\n", dropped);
            } else {
                LOGR("╟── UDP port %u:\n", udpSockPtrs_[i]->getBindPort());
                LOGR("║   ╟── TX bytes:   %lu\n", sendBytes);
                LOGR("║   ╟── TX errors:  %lu\n", sendErrs);
                LOGR("║   ╟── RX bytes:   %lu\n", recvBytes);
                LOGR("║   ╟── RX errors:  %lu\n", recvErrs);
                LOGR("║   ╙── dropped:    %lu\n", dropped);
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
    LOGR("╟── TX bytes:       %lu\n", totalSendBytes);
    LOGR("╟── TX errors:      %lu\n", totalSendErrs);
    LOGR("╟── RX bytes:       %lu\n", totalRecvBytes);
    LOGR("╟── RX errors:      %lu\n", totalRecvErrs);
    LOGR("╙── dropped:        %lu\n", totalDropped);

    LOGR("\n");

    LOGR("Total TAP:\n");
    LOGR("╟── write bytes:    %lu\n", TapDevPtr->getWriteBytes());
    LOGR("╟── write errors:   %lu\n", TapDevPtr->getWriteErrs());
    LOGR("╟── read  bytes:    %lu\n", TapDevPtr->getReadBytes());
    LOGR("╙── read  errors:   %lu\n", TapDevPtr->getReadErrs());
}
#endif

bool TapLan::run()
{
    if (!g_cfgData.running_)
        return false;

    if (g_cfgData.swPortIntvl_) {
        swPortThread_ = std::thread(&TapLan::swPortWrk, this);
        pthread_setname_np(swPortThread_.native_handle(), "swPortWrk");
    }

    if (g_cfgData.isAioEnable_) {
        uioIntfPtr_ = new AioIntf(TapDevPtr->getFd(), udpSockPtr_->getFd(), this);

        aioWrkThread_ = std::thread(&AioIntf::aioWrk, (AioIntf*)uioIntfPtr_);
        pthread_setname_np(aioWrkThread_.native_handle(), "aioWrk");
    } else {
        uioIntfPtr_ = new SioIntf(TapDevPtr->getFd(), udpSockPtr_->getFd(), this);

        udpWrkThread_ = std::thread(&SioIntf::udpWrk, (SioIntf*)uioIntfPtr_);
        pthread_setname_np(udpWrkThread_.native_handle(), "udpWrk");

        tapWrkThread_ = std::thread(&SioIntf::tapWrk, (SioIntf*)uioIntfPtr_);
        pthread_setname_np(tapWrkThread_.native_handle(), "tapWrk");
    }

    if (!g_cfgData.noSync_) {
        syncThread_ = std::thread(&TapLan::syncWrk, this);
        pthread_setname_np(syncThread_.native_handle(), "syncWrk");
    }

    return true;
}

bool TapLan::stop()
{
    if (!g_cfgData.running_)
        return false;

    g_cfgData.running_ = false;
    TapDevPtr->close();
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
    TapFd tapFd = TapDevPtr->getFd();

    char* payload = ctx->buf->payload;
    EthHdr eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& dstMac = eh.dst;
    Mac& srcMac = eh.src;
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == g_cfgData.mac_);

    if (g_cfgData.noSync_) {
        sockaddr_in6& srcAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
        nodeMgrPtr_->addNode(&srcAddr, srcMac);
    }

    if (g_cfgData.runMode_ == RunMode::server) {
        if (needBroadcast) {
            broadcastData(ctx);
            uioIntfPtr_->reqTapWrite(tapFd, ctx);
        } else if (!isSendToMe) {
            unicastData(ctx);
        } else {
            uioIntfPtr_->reqTapWrite(tapFd, ctx);
        }
    } else if (g_cfgData.runMode_ == RunMode::client) {
        uioIntfPtr_->reqTapWrite(tapFd, ctx);
    }
}

void TapLan::handleTapData(UioCtx* ctx)
{
    SockFd udpSendFd = udpSendSockPtr_.load()->getFd();

    char* payload = ctx->buf->payload;
    EthHdr& eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& dstMac = eh.dst;
    bool needBroadcast = eh.dst[0] & 0x01;

    if (g_cfgData.runMode_ == RunMode::server) {
        if (!needBroadcast) {
            unicastData(ctx);
        } else {
            broadcastData(ctx);
        }
    } else if (g_cfgData.runMode_ == RunMode::client) {
        if (!g_cfgData.noSync_ || nodeMgrPtr_->findNode(dstMac) || needBroadcast) {
            sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
            dstAddr = serverAddr_;
            uioIntfPtr_->reqUdpSend(udpSendFd, ctx);
        }
    }
}

void TapLan::unicastData(UioCtx* ctx)
{
    SockFd udpSendFd = udpSendSockPtr_.load()->getFd();

    EthHdr& eh = reinterpret_cast<EthHdr&>(*ctx->buf->payload);
    Mac& dstMac = eh.dst;
    auto n = nodeMgrPtr_->findNode(dstMac);
    if (n) {
        sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
        nodeMgrPtr_->setSockaddr(dstAddr, n);

        uioIntfPtr_->reqUdpSend(udpSendFd, ctx);
    }
}

void TapLan::broadcastData(UioCtx* ctx)
{
    SockFd udpSendFd = udpSendSockPtr_.load()->getFd();

    char* payload = ctx->buf->payload;
    EthHdr& eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& srcMac = eh.src;

    nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
        if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == g_cfgData.mac_)
            return;

        UioCtx* sendCtx = g_cfgData.isAioEnable_? uioIntfPtr_->acquireIoCtx(): ctx;
        if (g_cfgData.isAioEnable_ && sendCtx) {
            memcpy(sendCtx->buf->payload, payload, ctx->dataLen);
            sendCtx->dataLen = ctx->dataLen;
        }

        if (sendCtx) {
            sockaddr_in6& sendAddr = reinterpret_cast<sockaddr_in6&>(sendCtx->buf->addr);
            nodeMgrPtr_->setSockaddr(sendAddr, n);

            uioIntfPtr_->reqUdpSend(udpSendFd, sendCtx);
        }
    });
}
