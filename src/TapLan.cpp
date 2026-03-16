#include    "TapLan.hpp"

static const char* TAG = "[TapLan]";

TapLan::TapLan(): serverAddr_{}, udpSockPtr_(nullptr), udpSockPtrs_{},
                  nodeMgrPtr_(nullptr)
{
    if (g_cfgData.runMode == RunMode_Server) {
        LOGI(TAG, "We are running in server mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        serverAddr_.sin6_port = htons(g_cfgData.localPort);

        g_cfgData.isRunning = initUdpSockPtrs() && TapDevPtr->isFdVaild();

        if (g_cfgData.isRunning) {
            TapDevPtr->getMacAddr(g_cfgData.mac);
            NodeSPtr n = nodeMgrPtr_->addNode(&serverAddr_, g_cfgData.mac);
            TapDevPtr->setIPv4Addr(&n->ipv4Addr, g_cfgData.netNumLen);
        }
    } else if (g_cfgData.runMode == RunMode_Client) {
        LOGI(TAG, "We are running in client mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        memcpy(&serverAddr_.sin6_addr, &g_cfgData.remoteAddr, sizeof(in6_addr));
        serverAddr_.sin6_port = g_cfgData.remotePort;

        g_cfgData.isRunning = initUdpSockPtrs() && TapDevPtr->isFdVaild();

        if (g_cfgData.isRunning) {
            TapDevPtr->getMacAddr(g_cfgData.mac);
        }
    } else {
        // RunMode_None
    }
}

TapLan::~TapLan()
{
    stop();
}

UdpSocket* TapLan::getUdpSockPtr()
{
    UdpSocket* curUdpSockPtr = udpSockPtr_;
    if (g_cfgData.swPortIntvl) {
        std::time_t now = std::time(nullptr);
        uint16_t portIdx = (now / 60 / g_cfgData.swPortIntvl) % 4;
        for (int i = portIdx; i < portIdx + 4; ++i) {
            curUdpSockPtr = udpSockPtrs_[portIdx % 4];
            if (curUdpSockPtr)
                break;
        }
    }

    return curUdpSockPtr;
}

bool TapLan::initUdpSockPtrs()
{
    uint16_t startPort = g_cfgData.localPort - g_cfgData.localPort % 4;
    for (int i = 0; i < 4; ++i) {
        UdpSocket*& udpSockPtr = udpSockPtrs_[i];
        uint16_t port = startPort + i;
        if (!g_cfgData.swPortIntvl && port != g_cfgData.localPort) {
            continue;
        }

        udpSockPtr = new UdpSocket(port);
        if (!udpSockPtr->isFdValid()) {
            delete udpSockPtr;
            udpSockPtr = nullptr;
            LOGW(TAG, "Failed to bind [%u]port to udp socket.", port);
        }
        if (port == g_cfgData.localPort) {
            udpSockPtr_ = udpSockPtr;
        }
    }

    return (udpSockPtr_ && udpSockPtr_->isFdValid());
}

void TapLan::handleTapData(SioIntf& sioIntf, SioIntf::Ctx* ctx)
{
    if (ctx->dataLen > DATA_BUF_SIZE) {
        return ;
    }

    UdpSocket* udpSockPtr = getUdpSockPtr();
    SocketFd udpSendFd = static_cast<SocketFd>(*udpSockPtr);

    EthHdr& eh = reinterpret_cast<EthHdr&>(*ctx->buf);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;

    if (g_cfgData.runMode == RunMode_Server) {
        if (!needBroadcast) {
            NodeSPtr n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                nodeMgrPtr_->setSockaddr(ctx->addr, n);
                sioIntf.udpSend(udpSendFd, ctx);
            } else {
                udpSockPtr->incDropped(1);
            }
        } else {
            size_t sendCnt = 0;

            nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac)
                    return ;

                nodeMgrPtr_->setSockaddr(ctx->addr, n);
                sioIntf.udpSend(udpSendFd, ctx);
                ++sendCnt;
            }, false);

            if (sendCnt == 0) {
                udpSockPtr->incDropped(1);
            }
        }
    }
    else if (g_cfgData.runMode == RunMode_Client) {
        if (!g_cfgData.noSync || nodeMgrPtr_->findNode(dstMac) || needBroadcast) {
            memcpy(&ctx->addr, &serverAddr_, sizeof(sockaddr_in6));
            sioIntf.udpSend(udpSendFd, ctx);
        }
    } else {
        // RunMode_None
    }
}

void TapLan::handleUdpData(SioIntf& sioIntf, SioIntf::Ctx* ctx)
{
    if (ctx->dataLen > DATA_BUF_SIZE) {
        return ;
    }

    UdpSocket* udpSockPtr = getUdpSockPtr();
    SocketFd udpSendFd = static_cast<SocketFd>(*udpSockPtr);

    EthHdr eh = reinterpret_cast<EthHdr&>(*ctx->buf);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == g_cfgData.mac);

    if (g_cfgData.noSync) {
        nodeMgrPtr_->addNode(&ctx->addr, srcMac);
    }

    if (g_cfgData.runMode == RunMode_Server) {
        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, NodeSPtr n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == g_cfgData.mac)
                    return ;

                nodeMgrPtr_->setSockaddr(ctx->addr, n);
                sioIntf.udpSend(udpSendFd, ctx);
            };

            nodeMgrPtr_->forEach(broadcast, false);
            sioIntf.tapWrite(tapFd, ctx);
        } else if (!isSendToMe) {   // not broadcast && not send to me
            NodeSPtr n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                nodeMgrPtr_->setSockaddr(ctx->addr, n);
                sioIntf.udpSend(udpSendFd, ctx);
            }
        } else {                    // not broadcast && send to me
            sioIntf.tapWrite(tapFd, ctx);
        }
    } else if (g_cfgData.runMode == RunMode_Client) {
        sioIntf.tapWrite(tapFd, ctx);
    } else {
        // RunMode_None
    }
}

void TapLan::udpWrk()
{
    SocketFd udpRecvFd = static_cast<SocketFd>(*udpSockPtr_);
    SioIntf sioIntf;

    while (g_cfgData.isRunning) {
        SioIntf::Ctx* ctx = sioIntf.udpRecv(udpRecvFd);
        handleUdpData(sioIntf, ctx);
    }

    LOGI(TAG, "udpWrk has exited.");
}

void TapLan::tapWrk()
{
    TapFd tapReadFd = tapFd;
    SioIntf sioIntf;

    while (g_cfgData.isRunning) {
        SioIntf::Ctx* ctx = sioIntf.tapRead(tapReadFd);
        handleTapData(sioIntf, ctx);
    }

    LOGI(TAG, "tapWrk has exited.");
}

void TapLan::syncNodeStatus()
{
    if (g_cfgData.runMode == RunMode_Server) {
        nodeMgrPtr_->server();
    } else if (g_cfgData.runMode == RunMode_Client) {
        nodeMgrPtr_->client();
    } else {
        // RunMode_None
    }
}

void TapLan::showNodeStatus()
{
    if (!nodeMgrPtr_) {
        LOGI(TAG, "No other nodes have been obtained from the server.");
        return ;
    }

    LOGR("Status     TapLan MAC address    TapLan IP address    Public IP address\n");
//  LOGR("offline    00:00:00:00:00:00     255.255.255.255      aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb");

    auto printNodeStatus = [&](uint64_t m, NodeSPtr n) {
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
    };
    nodeMgrPtr_->forEach(printNodeStatus, false);
}

void TapLan::showStats()
{
    uint64_t totalSendBytes = 0, totalSendErrs = 0, totalRecvBytes = 0, totalRecvErrs = 0, totalDropped = 0;
    if (g_cfgData.swPortIntvl) {
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

bool TapLan::run()
{
    if (!g_cfgData.isRunning)
        return false;

    if (g_cfgData.isAioEnable) {
        aioWrkThread_ = std::thread(&TapLan::aioWrk, this);
        pthread_setname_np(aioWrkThread_.native_handle(), "aioWrk");
    } else {
        udpWrkThread_ = std::thread(&TapLan::udpWrk, this);
        pthread_setname_np(udpWrkThread_.native_handle(), "udpWrk");

        tapWrkThread_ = std::thread(&TapLan::tapWrk, this);
        pthread_setname_np(tapWrkThread_.native_handle(), "tapWrk");
    }

    if (!g_cfgData.noSync) {
        syncThread_ = std::thread(&TapLan::syncNodeStatus, this);
        pthread_setname_np(syncThread_.native_handle(), "syncWrk");
    }

    return true;
}

bool TapLan::stop()
{
    if (!g_cfgData.isRunning)
        return false;

    g_cfgData.isRunning = false;
    // FIXME: need close fd.

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
        LOGI(TAG, "syncWrk has exited.");
    }

    return true;
}

void TapLan::handleTapRead(AioIntf::Ctx* ctx) {
    if (ctx->bufLen <= 0) {
        ctx->owner->releaseAioCtx(ctx);
        LOGE(TAG, "Failed to read tap.[%s]", strerror(-ctx->bufLen));
    }

    SocketFd udpSendFd = static_cast<SocketFd>(*getUdpSockPtr());
    sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
    char* payload = ctx->buf->payload;

    EthHdr& eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;

    if (g_cfgData.runMode == RunMode_Server) {
        if (!needBroadcast) {
            auto n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                dstAddr.sin6_family = AF_INET6;
                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;

                AioIntfPtr->reqUdpSend(udpSendFd, ctx);
                return;
            }
        } else {
            nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac)
                    return;

                AioIntf::Ctx* sendCtx = AioIntfPtr->acquireAioCtx();
                if (sendCtx) {
                    sockaddr_in6& sendAddr = reinterpret_cast<sockaddr_in6&>(sendCtx->buf->addr);
                    memcpy(sendCtx->buf->payload, payload, ctx->bufLen);
                    sendCtx->bufLen = ctx->bufLen;

                    nodeMgrPtr_->setSockaddr(sendAddr, n);

                    AioIntfPtr->reqUdpSend(udpSendFd, sendCtx);
                }
            }, false);
        }
    } else if (g_cfgData.runMode == RunMode_Client) {
        if (!g_cfgData.noSync || nodeMgrPtr_->findNode(dstMac) || needBroadcast) {
            memcpy(&dstAddr, &serverAddr_, sizeof(sockaddr_in6));

            AioIntfPtr->reqUdpSend(udpSendFd, ctx);
            return;
        }
    }

    ctx->owner->releaseAioCtx(ctx);
}

void TapLan::handleTapWrite(AioIntf::Ctx* ctx) {
    if (ctx->bufLen <= 0) {
        ctx->owner->releaseAioCtx(ctx);
        LOGE(TAG, "Failed to write tap.[%s]", strerror(-ctx->bufLen));
    }

    ctx->owner->releaseAioCtx(ctx);
}

void TapLan::handleUdpRecv(AioIntf::Ctx* ctx) {
    if (ctx->bufLen < 0) {
        ctx->owner->releaseAioCtx(ctx);
        LOGE(TAG, "Failed to recv udp.[%s]", strerror(-ctx->bufLen));
    }

    SocketFd udpSendFd = static_cast<SocketFd>(*getUdpSockPtr());
    sockaddr_in6& srcAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
    char* payload = ctx->buf->payload;

    EthHdr eh = reinterpret_cast<EthHdr&>(*payload);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == g_cfgData.mac);

    if (g_cfgData.noSync) {
        nodeMgrPtr_->addNode(&srcAddr, srcMac);
    }

    if (g_cfgData.runMode == RunMode_Server) {
        if (needBroadcast) {
            nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == g_cfgData.mac)
                    return;

                AioIntf::Ctx* sendCtx = AioIntfPtr->acquireAioCtx();
                if (sendCtx) {
                    sockaddr_in6& sendAddr = reinterpret_cast<sockaddr_in6&>(sendCtx->buf->addr);
                    memcpy(sendCtx->buf->payload, payload, ctx->bufLen);
                    sendCtx->bufLen = ctx->bufLen;

                    nodeMgrPtr_->setSockaddr(sendAddr, n);

                    AioIntfPtr->reqUdpSend(udpSendFd, sendCtx);
                }
            }, false);

            AioIntfPtr->reqTapWrite(tapFd, ctx);
            return;

        } else if (!isSendToMe) {
            auto n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
                nodeMgrPtr_->setSockaddr(dstAddr, n);

                AioIntfPtr->reqUdpSend(udpSendFd, ctx);
                return;
            }
        } else {
            AioIntfPtr->reqTapWrite(tapFd, ctx);
            return;
        }
    } else if (g_cfgData.runMode == RunMode_Client) {
        AioIntfPtr->reqTapWrite(tapFd, ctx);
        return;
    }

    ctx->owner->releaseAioCtx(ctx);
}

void TapLan::handleUdpSend(AioIntf::Ctx* ctx) {
    if (ctx->bufLen < 0) {
        ctx->owner->releaseAioCtx(ctx);
        LOGE(TAG, "Failed to recv udp.[%s]", strerror(-ctx->bufLen));
    }

    ctx->owner->releaseAioCtx(ctx);
}

#ifdef      _WIN32
#if 0
void TapLan::sioWrk()
{
    SocketFd udpRecvFd = static_cast<SocketFd>(*udpSockPtr_);
    TapFd tapReadFd = tapFd;
    SioIntf::Ctx* ctx = nullptr;
    SioIntf sioIntf;

    sioIntf.init(udpRecvFd, tapReadFd);

    bool reverse = false;
    while (g_cfgData.isRunning) {
        DWORD result = WaitForMultipleObjects(NUMS_OF_EVENT, (sioIntf.events_ + reverse), FALSE, IO_WAIT_TIME * 1000);
        if (result == WAIT_FAILED) {
            DWORD err = GetLastError();
            LOGE(TAG, "Failed to wait for IO.[%s]", getErrMsg(err).c_str());
            break;
        }

        DWORD evId = result ^ reverse;
        switch (evId) {
        case EVENT_UDP_RECV:
            ctx = sioIntf.udpRecv(udpRecvFd);
            handleUdpData(sioIntf, ctx);
            break;

        case EVENT_TAP_READ:
            ctx = sioIntf.tapRead(tapReadFd);
            handleTapData(sioIntf, ctx);
            break;

        default:
            break;
        }

        reverse = !reverse;
    }

    LOGI(TAG, "sioWrk has exited.");
}
#endif

void TapLan::aioWrk()
{
    std::string errMsg;
    HANDLE& hIOCP = AioIntfPtr->hIOCP_;
    SocketFd udpRecvFd = static_cast<SocketFd>(*udpSockPtr_);

    if (!CreateIoCompletionPort(tapFd, hIOCP, (ULONG_PTR)this, 0)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to bind tap to IOCP.[%s]", errMsg.c_str());
        g_cfgData.isRunning = false;
    }
    if (g_cfgData.swPortIntvl) {
        for (int i = 0; i < 4; ++i) {
            SocketFd udpFd = static_cast<SocketFd>(*udpSockPtrs_[i]);
            if (!CreateIoCompletionPort((HANDLE)udpFd, hIOCP, (ULONG_PTR)this, 0)) {
                errMsg = getErrMsg(GetLastError());
                LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
                g_cfgData.isRunning = false;
            }
        }
    } else {
        if (!CreateIoCompletionPort((HANDLE)udpRecvFd, hIOCP, (ULONG_PTR)this, 0)) {
            errMsg = getErrMsg(GetLastError());
            LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
            g_cfgData.isRunning = false;
        }
    }

    // for (int i = 0; i < MAX_READ_REQ; ++i) {
    //     AioIntfPtr->reqTapRead(tapFd);
    // }
    // for (int i = 0; i < MAX_RECV_REQ; ++i) {
    //     AioIntfPtr->reqUdpRecv(udpRecvFd);
    // }
    AioIntfPtr->reqTapReadMultishot(tapFd);
    AioIntfPtr->reqUdpRecvMultishot(udpRecvFd);

    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED lpOverlapped;
    DWORD err;
    while (g_cfgData.isRunning) {
        BOOL ok = GetQueuedCompletionStatus(
            hIOCP,
            &bytes,
            &key,
            &lpOverlapped,
            IO_WAIT_TIME * 1000
        );

        if (!lpOverlapped)
            continue;

        AioIntf::Ctx* ctx = CONTAINING_RECORD(lpOverlapped, AioIntf::Ctx, ol);
        if (ok) {
            ctx->bufLen = bytes;
        } else {
            err = GetLastError();
            if (err == WAIT_TIMEOUT)
                continue;

            errMsg = getErrMsg(err);
            ctx->bufLen = -err;
            LOGE(TAG, "Failed to GQCS.[%s]", errMsg.c_str());
        }

        switch (ctx->token) {
        case TOKEN_UDP_RECV:
            handleUdpRecv(ctx);
            break;

        case TOKEN_TAP_READ:
            handleTapRead(ctx);
            break;

        case TOKEN_TAP_WRITE:
            handleTapWrite(ctx);
            break;

        case TOKEN_UDP_SEND:
            handleUdpSend(ctx);
            break;
        
        default:
            break;
        }
    }

    LOGI(TAG, "aioWrk has exited.");
}

#elif       __linux__
#if 0
void TapLan::sioWrk()
{
    TapFd tapReadFd = tapFd;
    SocketFd udpRecvFd = static_cast<SocketFd>(*udpSockPtr_);
    SioIntf sioIntf;
    pollfd pfds[NUMS_OF_EVENT];
    pfds[EVENT_UDP_RECV] = {udpRecvFd, POLLIN, 0};
    pfds[EVENT_TAP_READ] = {tapReadFd, POLLIN, 0};

    while (g_cfgData.isRunning) {
        int res = poll(pfds, NUMS_OF_EVENT, IO_WAIT_TIME);
        if (res < 0) {
            LOGE(TAG, "Failed to poll events.[%s]", strerror(errno));
            continue;
        } else if (res == 0) {
            continue;
        }

        if (pfds[EVENT_UDP_RECV].revents & POLLIN) {
            SioIntf::Ctx* ctx = sioIntf.udpRecv(udpRecvFd);
            handleUdpData(sioIntf, ctx);
            pfds[EVENT_UDP_RECV].revents &= ~POLLIN;
        }
        if (pfds[EVENT_TAP_READ].revents & POLLIN) {
            SioIntf::Ctx* ctx = sioIntf.tapRead(tapReadFd);
            handleTapData(sioIntf, ctx);
            pfds[EVENT_TAP_READ].revents &= ~POLLIN;
        }
    }
}
#endif

void TapLan::aioWrk()
{
    io_uring* ring = &AioIntfPtr->ring_;
    SocketFd udpRecvFd = static_cast<SocketFd>(*udpSockPtr_);

    // for (int i = 0; i < MAX_READ_REQ; ++i) {
    //     AioIntfPtr->reqTapRead(tapFd);
    // }
    // for (int i = 0; i < MAX_RECV_REQ; ++i) {
    //     AioIntfPtr->reqUdpRecv(udpRecvFd);
    // }
    AioIntfPtr->reqTapReadMultishot(tapFd);
    AioIntfPtr->reqUdpRecvMultishot(udpRecvFd);
    io_uring_submit(ring);

    io_uring_cqe *cqe;
    __kernel_timespec timeout{IO_WAIT_TIME, 0};
    size_t maxCqeBatch = std::min(MAX_READ_REQ, MAX_RECV_REQ) / 2;
    while (g_cfgData.isRunning) {
        int ret = io_uring_wait_cqe_timeout(ring, &cqe, &timeout);
        if (ret < 0 && ret != -ETIME) {
            LOGF(TAG, "Failed to wait cqe.[%s]", strerror(-ret));
            break;
        }

        unsigned head;
        unsigned cqeCnt = 0;
        io_uring_for_each_cqe(ring, head, cqe) {
            ++cqeCnt;

            AioIntf::Ctx* ctx = reinterpret_cast<AioIntf::Ctx*>(cqe->user_data);
            ctx->bufLen = cqe->res;

            switch (ctx->token) {
            case TOKEN_UDP_RECV_MULTISHOT: {
                if (!(cqe->flags & IORING_CQE_F_MORE)) {
                    LOGE(TAG, "UDP recvmsg multishot stop.[%d]", strerror(-cqe->res));
                    AioIntfPtr->reqUdpRecvMultishot(udpRecvFd);
                }

                size_t idx = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                AioIntf::Ctx* realCtx = AioIntfPtr->acquireAioCtx(idx);
                realCtx->bufLen = cqe->res;
                handleUdpRecv(realCtx);

                break;
            }

            case TOKEN_UDP_RECV:
                handleUdpRecv(ctx);
                break;

            case TOKEN_UDP_SEND:
                // if ((cqe->flags & IORING_CQE_F_NOTIF)
                //     || !(cqe->flags & IORING_CQE_F_MORE))
                handleUdpSend(ctx);
                break;

            case TOKEN_TAP_READ:
                handleTapRead(ctx);
                break;

            case TOKEN_TAP_WRITE:
                handleTapWrite(ctx);
                break;

            default:
                break;
            }

            if (cqeCnt >= maxCqeBatch) {
                io_uring_cq_advance(ring, cqeCnt);
                io_uring_submit(ring);
                cqeCnt = 0;
            }
        }
        io_uring_cq_advance(ring, cqeCnt);
        io_uring_submit(ring);
    }

    LOGI(TAG, "aioWrk has exited.");
}
#endif
