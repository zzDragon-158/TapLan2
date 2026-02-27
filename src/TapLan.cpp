#include    "TapLan.hpp"

static const char* TAG = "[TapLan]";
ConfigDataT TapLan::config_;

TapLan::TapLan(): serverAddr_{}, udpSockPtr_(nullptr), udpSockPtrArr_{},
                nodeMgrPtr_(nullptr), recvThreadName_("recvWorker"),
                sendThreadName_("sendWorker"), syncThreadName_("syncWorker")
{
    if (config_.runMode == RunMode_Server) {
        LOGI(TAG, "We are running in server mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        serverAddr_.sin6_port = htons(config_.localPort);

        initUdpSockPtr();

        config_.isRunning = udpSockPtr_->isFdValid() && TapDevPtr->isFdVaild();

        if (config_.isRunning) {
            TapDevPtr->getMacAddr(config_.mac);
            std::shared_ptr<Node> n = nodeMgrPtr_->addNode(&serverAddr_, config_.mac);
            TapDevPtr->setIPv4Addr(&n->ipv4Addr, config_.netNumLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        LOGI(TAG, "We are running in client mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        memcpy(&serverAddr_.sin6_addr, &config_.remoteAddr, sizeof(in6_addr));
        serverAddr_.sin6_port = config_.remotePort;

        initUdpSockPtr();

        config_.isRunning = udpSockPtr_->isFdValid() && TapDevPtr->isFdVaild();

        if (config_.isRunning) {
            TapDevPtr->getMacAddr(config_.mac);
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
    if (config_.switchPortInterval) {
        std::time_t now = std::time(nullptr);
        uint16_t portOffset = (now / 60 / config_.switchPortInterval) % 4;
        curUdpSockPtr = udpSockPtrArr_[portOffset];
    }

    return curUdpSockPtr;
}

void TapLan::initUdpSockPtr()
{
    if (config_.switchPortInterval) {
        uint16_t startPort = config_.localPort - config_.localPort % 4;
        for (int i = 0; i < 4; ++i) {
            uint16_t port = startPort + i;
            udpSockPtrArr_[i] = new UdpSocket(port);
            if (!udpSockPtrArr_[i]->isFdValid()) {
                config_.switchPortInterval = 0;
            }

            if (port == config_.localPort) {
                udpSockPtr_ = udpSockPtrArr_[i];
            }
        }
    } else {
        udpSockPtr_ = new UdpSocket(config_.localPort);
    }

    if (!udpSockPtr_) {
        udpSockPtr_ = new UdpSocket(config_.localPort);
    }
}

void TapLan::handleTapData(uint8_t* buf, size_t bufLen)
{
    sockaddr_in6 dstAddr{};
    dstAddr.sin6_family = AF_INET6;

    UdpSocket* udpSockPtr = getUdpSockPtr();

    EtherHeader& eh = reinterpret_cast<EtherHeader&>(*buf);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;
    if (config_.runMode == RunMode_Server) {
        if (!needBroadcast) {
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr->sendTo(buf, bufLen, (const sockaddr*)&dstAddr, sizeof(dstAddr));
            } else {
                udpSockPtr->incDropped(1);
            }
        } else {
            size_t sendCnt = 0;
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
                ++sendCnt;
            }, false);
            if (sendCnt == 0) {
                udpSockPtr->incDropped(1);
            }
        }
    }
    else if (config_.runMode == RunMode_Client) {
        if (!config_.noServerMode || nodeMgrPtr_->findNode(dstMac) || needBroadcast)
            udpSockPtr->sendTo(buf, bufLen, (const sockaddr*)&serverAddr_, sizeof(serverAddr_));
    } else {
        // RunMode_None
    }
}

void TapLan::readTapData()
{
    uint8_t tapRxBuf[65536];

    while (config_.isRunning) {
        ssize_t readBytes = TapDevPtr->read(tapRxBuf, sizeof(tapRxBuf), 3000);
        if (readBytes < ETHERNET_HEADER_LEN) {
            continue;
        }

        handleTapData(tapRxBuf, readBytes);
    }

    std::cout << "Thread " << sendThreadName_ << " has exited." << std::endl;
}

void TapLan::handleSockData(uint8_t* buf, size_t bufLen, sockaddr_in6& srcAddr)
{
    UdpSocket* udpSockPtr = getUdpSockPtr();

    EtherHeader eh = reinterpret_cast<EtherHeader&>(*buf);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == config_.mac);
    if (config_.runMode == RunMode_Server) {
        sockaddr_in6 dstAddr{};
        dstAddr.sin6_family = AF_INET6;

        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == config_.mac)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
            };

            nodeMgrPtr_->forEach(broadcast, false);
            TapDevPtr->write(buf, bufLen);
        } else if (!isSendToMe) {   // not broadcast && not send to me
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (!n) {
                return ;
            }
            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            udpSockPtr->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(sockaddr_in6));
        } else {                    // not broadcast && send to me
            TapDevPtr->write(buf, bufLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        if (config_.noServerMode && !nodeMgrPtr_->findNode(srcMac)) {
            nodeMgrPtr_->addNode(&serverAddr_, srcMac);
        }
        TapDevPtr->write(buf, bufLen);
    } else {
        // RunMode_None
    }
}

void TapLan::recvSockData()
{
    uint8_t udpRxBuf[65536];

    sockaddr_in6 srcAddr;
    socklen_t srcAddrLen = sizeof(srcAddr);

    sockaddr_in6 dstAddr;
    memset(&dstAddr, 0, sizeof(dstAddr));
    dstAddr.sin6_family = AF_INET6;

    TapLanPollFd pfds[4];
    if (config_.switchPortInterval) {
        for (int i = 0; i < 4; ++i) {
            pfds[i] = { static_cast<SocketFd>(*udpSockPtrArr_[i]), POLLIN, 0 };
        }
    }

    while (config_.isRunning) {
        if (!config_.switchPortInterval) {
            ssize_t recvBytes = udpSockPtr_->recvFrom(udpRxBuf, sizeof(udpRxBuf), (sockaddr*)&srcAddr, &srcAddrLen);
            if (recvBytes <= ETHERNET_HEADER_LEN) {
                continue;
            }

            handleSockData(udpRxBuf, recvBytes, srcAddr);
            continue;
        } else {
            int pollCnt = TapLanPoll(pfds, 4, 3000);
            if (pollCnt < 0) {
                LOGE(TAG, "TapLanPoll failed.");
                continue;
            } else if (pollCnt == 0) {
                continue;
            }
            for (int i = 0; i < 4; ++i) {
                if (pfds[i].revents != 0) {
                    ssize_t recvBytes = udpSockPtrArr_[i]->recvFrom(udpRxBuf, sizeof(udpRxBuf), (sockaddr*)&srcAddr, &srcAddrLen);
                    if (recvBytes <= ETHERNET_HEADER_LEN) {
                        continue;
                    }

                    handleSockData(udpRxBuf, recvBytes, srcAddr);
                }
            }
        }
    }

    std::cout << "Thread " << recvThreadName_ << " has exited." << std::endl;
}

void TapLan::syncNodeStatus()
{
    if (config_.runMode == RunMode_Server) {
        nodeMgrPtr_->server();
    } else if (config_.runMode == RunMode_Client) {
        nodeMgrPtr_->client();
    } else {
        // RunMode_None
    }

    std::cout << "Thread " << syncThreadName_ << " has exited." << std::endl;
}

void TapLan::showNodeStatus()
{
    if (!nodeMgrPtr_) {
        LOGI(TAG, "No other nodes have been obtained from the server.");
        return ;
    }

    LOGR("Status     TapLan MAC address    TapLan IP address    Public IP address\n");
//  LOGR("offline    00:00:00:00:00:00     255.255.255.255      aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb");

    auto printNodeStatus = [&](uint64_t m, std::shared_ptr<Node> n) {
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
    if (config_.switchPortInterval) {
        LOGR("Each UDP:\n");
        for (int i = 0; i < 4; ++i) {
            uint64_t sendBytes = udpSockPtrArr_[i]->getSendBytes(),
                     sendErrs = udpSockPtrArr_[i]->getSendErrors(),
                     recvBytes = udpSockPtrArr_[i]->getRecvBytes(),
                     recvErrs = udpSockPtrArr_[i]->getRecvErrors(),
                     dropped = udpSockPtrArr_[i]->getDropped();

            if (i == 3) {
                LOGR("╙── UDP port %u:\n", udpSockPtrArr_[i]->getBindPort());
                LOGR("    ╟── TX bytes:   %lu\n", sendBytes);
                LOGR("    ╟── TX errors:  %lu\n", sendErrs);
                LOGR("    ╟── RX bytes:   %lu\n", recvBytes);
                LOGR("    ╟── RX errors:  %lu\n", recvErrs);
                LOGR("    ╙── dropped:    %lu\n", dropped);
            } else {
                LOGR("╟── UDP port %u:\n", udpSockPtrArr_[i]->getBindPort());
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
    if (!config_.isRunning)
        return false;

    if (config_.isAioEnable) {
#ifdef      _WIN32
        recvThread_ = std::thread(&TapLan::iocpWrk, this);
#else
        sendThread_ = std::thread(&TapLan::uring_read_tap_wrk, this);
        recvThread_ = std::thread(&TapLan::uring_recv_udp_wrk, this);
#endif
    } else {
        sendThread_ = std::thread(&TapLan::readTapData, this);
        recvThread_ = std::thread(&TapLan::recvSockData, this);
    }

    pthread_setname_np(sendThread_.native_handle(), sendThreadName_);
    pthread_setname_np(recvThread_.native_handle(), recvThreadName_);

    if (!config_.noServerMode) {
        syncThread_ = std::thread(&TapLan::syncNodeStatus, this);
        pthread_setname_np(syncThread_.native_handle(), syncThreadName_);
    }

    return true;
}

bool TapLan::stop()
{
    if (!config_.isRunning)
        return false;

    config_.isRunning = false;
    // FIXME: PostQueuedCompletionStatus(hIOCP_, 0, (ULONG_PTR)IOCP_EXIT_MAGIC, NULL);
    if (sendThread_.joinable())
        sendThread_.join();
    if (recvThread_.joinable())
        recvThread_.join();
    if (syncThread_.joinable())
        syncThread_.join();

    return true;
}

#ifdef      _WIN32
#define     IOCP_EXIT_MAGIC                         0xDEADBEEF
#define     AIO_BUF_SIZE        2048
void TapLan::reqTapRead(IOContext* ctx)
{
    ctx->token = TOKEN_TAP_READ;

    BOOL ok = ReadFile(tapFd, ctx->buf, AIO_BUF_SIZE, NULL, &ctx->overlapped);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            ctx->owner->release(ctx);
            LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
        }
    }
}

int TapLan::reqTapWrite(IOContext* ctx)
{
    ctx->token = TOKEN_TAP_WRITE;

    BOOL ok = WriteFile(tapFd, ctx->buf, (DWORD)ctx->bufLen, NULL, &ctx->overlapped);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
            ctx->owner->release(ctx);
            return -1;
        }
    }

    return 0;
}

void TapLan::reqUdpRecv(IOContext* ctx)
{
    ctx->token = TOKEN_UDP_RECV;

    WSABUF wsaBuf;
    wsaBuf.buf = (char*)ctx->buf;
    wsaBuf.len = AIO_BUF_SIZE;

    DWORD flags = 0;
    int result = WSARecv(
        static_cast<SocketFd>(*udpSockPtr_),
        &wsaBuf,
        1,
        NULL,
        &flags,
        &ctx->overlapped, 
        NULL
    );
    if (result == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            ctx->owner->release(ctx);
            LOGE(TAG, "Failed to recv udp.[%s]", getErrMsg(err).c_str());
        }
    }
}

int TapLan::reqUdpSend(IOContext* ctx, sockaddr_in6* addr)
{
    // 1. 设置标识，确保发送完成后能正确回到写池
    ctx->token = TOKEN_UDP_SEND;

    // 2. 准备 WSABUF
    WSABUF wsaBuf;
    wsaBuf.buf = (char*)ctx->buf;
    wsaBuf.len = (ULONG)ctx->bufLen; // 注意：这里应该是实际要发送的字节数，而不是 Buffer 最大长度

    // 3. 发起异步发送
    // WSASendTo 即使对于 IPv4 也可以接受 sockaddr_in6 结构的指针（只要族属性正确）
    int result = WSASendTo(
        static_cast<SocketFd>(*udpSockPtr_),
        &wsaBuf, 
        1, 
        NULL, 
        0, 
        (const sockaddr*)addr, 
        sizeof(sockaddr_in6), 
        &ctx->overlapped, 
        NULL
    );

    if (result == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            // 发送失败，立即归还 Context 到写池
            ctx->owner->release(ctx);
            LOGE(TAG, "Failed to send udp.[%s]", getErrMsg(err).c_str());
            return -1;
        }
    }

    return 0;
}

void TapLan::handleTapRead(IOContext* ctx) {
    // 1. 检查读取是否成功
    if (ctx->overlapped.Internal != 0) { // 检查错误码
        reqTapRead(ctx); // 重新挂起读取
        return;
    }

    ctx->bufLen = ctx->overlapped.InternalHigh; // 获取实际读取字节数

    EtherHeader& eh = reinterpret_cast<EtherHeader&>(*ctx->buf);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;

    if (config_.runMode == RunMode_Server) {
        if (!needBroadcast) {
            auto n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                // 单播：直接原地转换 ctx，零拷贝发送
                sockaddr_in6 dstAddr{};
                dstAddr.sin6_family = AF_INET6;
                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                
                reqUdpSend(ctx, &dstAddr); 
                // 注意：此时不能调 reqTapRead，ctx 已经流转到 UDP 发送了
                return; 
            }
        } else {
            // 广播：不能零拷贝，因为需要同时发送给多个目标
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac) return;

                // 从写池申请新的 ctx，拷贝数据后发送
                IOContext* sendCtx = ioBufs_->acquire();
                if (sendCtx) {
                    memcpy(sendCtx->buf, ctx->buf, ctx->bufLen);
                    
                    sockaddr_in6 dstAddr{};
                    dstAddr.sin6_family = AF_INET6;
                    memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                    dstAddr.sin6_port = n->ipv6Port;
                    reqUdpSend(sendCtx, &dstAddr);
                }
            }, false);
        }
    } else if (config_.runMode == RunMode_Client) {
        if (!config_.noServerMode || nodeMgrPtr_->findNode(dstMac) || needBroadcast) {
            reqUdpSend(ctx, &serverAddr_);
            return;
        }
    }

    // 如果没有走 reqUdpSend (比如没找到节点或丢包)，则回收并重新读取
    reqTapRead(ctx);
}

void TapLan::handleTapWrite(IOContext* ctx) {
    // 写入虚拟网卡完成，归还到 UDP 的接收池
    ctx->owner->release(ctx);

    // 如果它是 UDP 接收池的常驻 ctx，重新发起接收
    if (ctx->owner == recvBufs_) {
        reqUdpRecv(ctx);
    }
}

void TapLan::handleUdpRecv(IOContext* ctx) {
    // 1. 检查异步读取状态
    if (ctx->overlapped.Internal != 0) {
        reqUdpRecv(ctx); // 失败则重新挂起接收
        return;
    }

    ctx->bufLen = ctx->overlapped.InternalHigh;

    EtherHeader eh = reinterpret_cast<EtherHeader&>(*ctx->buf);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == config_.mac);

    if (config_.runMode == RunMode_Server) {
        if (needBroadcast) {
            // --- 广播逻辑 ---
            // 1. 发给其他节点 (从写池申请新 ctx 进行拷贝发送)
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == config_.mac)
                    return;

                IOContext* sendCtx = ioBufs_->acquire();
                if (sendCtx) {
                    sendCtx->token = TOKEN_UDP_SEND;
                    memcpy(sendCtx->buf, ctx->buf, ctx->bufLen);
                    sendCtx->bufLen = ctx->bufLen;
                    
                    sockaddr_in6 dstAddr{};
                    dstAddr.sin6_family = AF_INET6;
                    memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                    dstAddr.sin6_port = n->ipv6Port;
                    reqUdpSend(sendCtx, &dstAddr);
                }
            }, false);

            // 2. 发给本地 (直接用当前的 ctx 写入 TAP)
            reqTapWrite(ctx);
            return; // ctx 已流转到 TOKEN_TAP_WRITE

        } else if (!isSendToMe) {
            // --- 转发逻辑 (Client A -> Server -> Client B) ---
            auto n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                sockaddr_in6 dstAddr{};
                dstAddr.sin6_family = AF_INET6;
                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                
                // 零拷贝转发：直接将 Recv 的 ctx 变成 Send 的 ctx
                reqUdpSend(ctx, &dstAddr);
                return; // ctx 已流转到 TOKEN_UDP_SEND
            }
        } else {
            // --- 发给本地单播 ---
            reqTapWrite(ctx);
            return;
        }
    } else if (config_.runMode == RunMode_Client) {
        // --- 客户端模式 ---
        if (config_.noServerMode && !nodeMgrPtr_->findNode(srcMac)) {
            // 注意：这里需要从异步结果中获取 srcAddr，
            // 如果你使用了 WSARecv，你可能需要改回 WSARecvFrom 来记录谁发的。
            // 假设你已经有了 srcAddr：
            // nodeMgrPtr_->addNode(&srcAddr, srcMac); 
        }
        reqTapWrite(ctx);
        return;
    }

    // 如果没有任何转发或写入动作，回收并继续接收
    reqUdpRecv(ctx);
}

void TapLan::handleUdpSend(IOContext* ctx) {
    // 无论发送成功失败，这个请求都已经结束了
    // 直接利用你之前设计的地址归还机制
    ctx->owner->release(ctx);

    // 如果这个 ctx 是从 TAP 读池过来的（单播零拷贝场景）
    // 且它是 TAP 的常驻读取 ctx，则需要重新发起 Read
    if (ctx->owner == readBufs_) {
        reqTapRead(ctx);
    } else if (ctx->owner == recvBufs_) {
        reqUdpRecv(ctx);
    }
}

void TapLan::iocpWrk()
{
    const int AIO_BUF_NUM = 32;
    extern TapFd tapFd;
    SocketFd udpFd = static_cast<SocketFd>(*udpSockPtr_);
    readBufs_ = new IOPool(AIO_BUF_NUM);
    recvBufs_ = new IOPool(AIO_BUF_NUM);
    ioBufs_ = new IOPool(512);
    std::string errMsg;
    hIOCP_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (hIOCP_ == NULL) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to create IOCP.[%s]", errMsg.c_str());
        config_.isRunning = false;
    }
    if (!CreateIoCompletionPort(tapFd, hIOCP_, (ULONG_PTR)this, 0)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to bind tap to IOCP.[%s]", errMsg.c_str());
        config_.isRunning = false;
    }
    if (!CreateIoCompletionPort((HANDLE)udpFd, hIOCP_, (ULONG_PTR)this, 0)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
        config_.isRunning = false;
    }

    for (int i = 0; i < AIO_BUF_NUM; ++i) {
        IOContext* readCtx = readBufs_->acquire();
        reqTapRead(readCtx);

        IOContext* recvCtx = recvBufs_->acquire();
        reqUdpRecv(recvCtx);
    }

    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED lpOverlapped;
    while (config_.isRunning) {
        BOOL res = GetQueuedCompletionStatus(
            hIOCP_,
            &bytes,
            &key,
            &lpOverlapped,
            INFINITE 
        );
        if (!lpOverlapped) {
            if (key == (ULONG_PTR)IOCP_EXIT_MAGIC) {
                config_.isRunning = false;
                break;
            }
            // TODO: add log
            continue;
        }

        IOContext* ctx = CONTAINING_RECORD(lpOverlapped, IOContext, overlapped);
        switch (ctx->token)
        {
        case TOKEN_TAP_READ:
            handleTapRead(ctx);
            break;

        case TOKEN_TAP_WRITE:
            handleTapWrite(ctx);
            break;

        case TOKEN_UDP_RECV:
            handleUdpRecv(ctx);
            break;

        case TOKEN_UDP_SEND:
            handleUdpSend(ctx);
            break;
        
        default:
            break;
        }
    }
}

#elif       __linux__
void TapLan::prep_tap_read(uint32_t buf_id) {
    uring_send_msg *msg = (uring_send_msg *)(read_bufs + (buf_id * TAP_BUF_SIZE));
    io_uring_sqe* sqe = io_uring_get_sqe(&tap_uring);
    io_uring_prep_read_fixed(sqe, tapFd, msg->data, TAP_BUF_SIZE - MSG_HDR_SIZE, 0, buf_id);
    sqe->user_data = (TOKEN_TAP_READ << 16) | buf_id;
};

void TapLan::handle_tap_read(io_uring_cqe *cqe)
{
    uint16_t buf_id = (uint16_t)cqe->user_data;
    if (cqe->res <= 0) {
        if (cqe->res != -EAGAIN && cqe->res != 0) {
            TapDevPtr->incReadErrs(1);
            LOGE(TAG, "handle tap read failed.[%s]", strerror(-cqe->res));
        }
        prep_tap_read(buf_id);
        return ;
    }

    TapDevPtr->incReadBytes(cqe->res);
    UdpSocket* udpSockPtr = getUdpSockPtr();
    SocketFd udp_fd = static_cast<SocketFd>(*udpSockPtr);
    uring_send_msg *msg = (uring_send_msg *)(read_bufs + (buf_id * TAP_BUF_SIZE));
    if (config_.runMode == RunMode_Server) {
        EtherHeader& eh = reinterpret_cast<EtherHeader&>(*msg->data);
        Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
        Mac& srcMac = reinterpret_cast<Mac&>(eh.src);

        bool needBroadcast = eh.dst[0] & 0x01;
        if (!needBroadcast) {
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                msg->nums_of_addr = 1;
                sockaddr_in6& addr = msg->addrs[0];
                memset(&addr, 0, sizeof(sockaddr_in6));
                addr.sin6_family = AF_INET6;
                memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                addr.sin6_port = n->ipv6Port;

                io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
                io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
                sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
            } else {
                prep_tap_read(buf_id);
            }
        } else {
            msg->nums_of_addr = 0;
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac)
                    return ;

                sockaddr_in6& addr = msg->addrs[msg->nums_of_addr++];
                memset(&addr, 0, sizeof(sockaddr_in6));
                addr.sin6_family = AF_INET6;
                memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                addr.sin6_port = n->ipv6Port;

                io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
                io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
                sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
            }, false);

            if (msg->nums_of_addr == 0) {
                udpSockPtr->incDropped(1);
                prep_tap_read(buf_id);
            }
        }
    } else if (config_.runMode == RunMode_Client) {
        msg->nums_of_addr = 1;
        sockaddr_in6& addr = msg->addrs[0];
        memcpy(&addr, &serverAddr_, sizeof(sockaddr_in6));

        io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
        io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&serverAddr_, sizeof(sockaddr_in6));
        sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
    } else {
        // RunMode_None
    }

    return ;
}

void TapLan::uring_read_tap_wrk()
{
    int ret;
    SocketFd udp_fd = static_cast<SocketFd>(*udpSockPtr_);
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    ret = io_uring_queue_init_params(QD, &tap_uring, &params);
    if (ret < 0) {
        LOGF(TAG, "TAP queue init params failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    read_bufs = (uint8_t *)mmap(NULL, TAP_BUF_NUM * TAP_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (read_bufs == MAP_FAILED) {
        posix_memalign((void **)&read_bufs, 4096, TAP_BUF_NUM * TAP_BUF_SIZE);
    }

    iovec iovs[TAP_BUF_NUM];
    for (size_t i = 0; i < TAP_BUF_NUM; ++i) {
        iovs[i].iov_base = read_bufs + i * TAP_BUF_SIZE;
        iovs[i].iov_len = TAP_BUF_SIZE;
    }
    ret = io_uring_register_buffers(&tap_uring, iovs, TAP_BUF_NUM);
    if (ret < 0) {
        LOGF(TAG, "TAP register buffers failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    for (size_t idx = 0; idx < TAP_BUF_NUM; ++idx) {
        prep_tap_read(idx);
    }
    io_uring_submit(&tap_uring);

    auto handle_udp_send = [&](io_uring_cqe *cqe) {
        UdpSocket* udpSockPtr = getUdpSockPtr();
        uint32_t buf_id = (uint16_t)cqe->user_data;
        if (cqe->res < 0) {
            udpSockPtr->incSendErrs(1);
            LOGE(TAG, "TAP handle udp send failed.[%s]", strerror(-cqe->res));
        }

        udpSockPtr->incSendBytes(cqe->res);
        uring_send_msg *msg = (uring_send_msg *)(read_bufs + (buf_id * TAP_BUF_SIZE));
        if (!--msg->nums_of_addr)
            prep_tap_read(buf_id);
    };

    io_uring_cqe *cqe;
    __kernel_timespec timeout{3, 0};
    while (config_.isRunning) {
        ret = io_uring_wait_cqe_timeout(&tap_uring, &cqe, &timeout);
        if (ret < 0 && ret != -ETIME) {
            LOGF(TAG, "TAP wait cqe failed.[%s]", strerror(-ret));
            break;
        }

        unsigned head;
        unsigned cqe_count = 0;
        io_uring_for_each_cqe(&tap_uring, head, cqe) {
            ++cqe_count;

            switch (cqe->user_data >> 16)
            {
            case TOKEN_UDP_RECV:
                break;

            case TOKEN_TAP_READ:
                handle_tap_read(cqe);
                break;

            case TOKEN_TAP_WRITE:
                break;

            case TOKEN_UDP_SEND:
                handle_udp_send(cqe);
                break;
            
            default:
                break;
            }
        }

        io_uring_cq_advance(&tap_uring, cqe_count);
        io_uring_submit(&tap_uring);
    }

    io_uring_queue_exit(&tap_uring);
    config_.isRunning = false;
    std::cout << "Thread " << sendThreadName_ << " has exited." << std::endl;
    return ;
}

void TapLan::prep_udp_recv() {
    SocketFd udp_fd = static_cast<SocketFd>(*udpSockPtr_);
    io_uring_sqe* sqe = io_uring_get_sqe(&udp_uring);
    io_uring_prep_recv_multishot(sqe, udp_fd, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = UDP_BUF_GRP_ID;
    sqe->user_data = (TOKEN_UDP_RECV << 16);
}

int TapLan::handle_udp_recv(io_uring_cqe *cqe) {
    UdpSocket* udpSockPtr = getUdpSockPtr();
    SocketFd udp_fd = static_cast<SocketFd>(*udpSockPtr);

    if (!(cqe->flags & IORING_CQE_F_MORE)) {
        LOGE(TAG, "UDP recvmsg multishot stop.[%d]", strerror(-cqe->res));
        prep_udp_recv();
    }

    if (cqe->res < 0) {
        udpSockPtr->incRecvErrs(1);
        LOGE(TAG, "UDP handle udp recv failed.[%s]", strerror(-cqe->res));
        return 1;
    } else if (cqe->res == 0) {
        udpSockPtr->incRecvErrs(1);
        return 1;
    } else {
        udpSockPtr->incRecvBytes(cqe->res);
    }

    uint32_t buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
    uring_send_msg *msg = (uring_send_msg *)(recv_bufs + (buf_id * UDP_BUF_SIZE));
    EtherHeader eh = reinterpret_cast<EtherHeader&>(*msg->data);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == config_.mac);
    if (config_.runMode == RunMode_Server) {
        if (needBroadcast) {        // broadcast
            msg->nums_of_addr = 0;
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == config_.mac)
                    return ;

                sockaddr_in6& addr = msg->addrs[msg->nums_of_addr++];
                memset(&addr, 0, sizeof(sockaddr_in6));
                addr.sin6_family = AF_INET6;
                memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                addr.sin6_port = n->ipv6Port;

                io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
                io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
                sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
            }, false);

            ++msg->nums_of_addr;
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write_fixed(sqe, tapFd, msg->data, cqe->res, 0, buf_id);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        } else if (!isSendToMe) {   // not broadcast && not send to me
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (!n) {
                udpSockPtr->incDropped(1);
                return 1;
            }
            msg->nums_of_addr = 1;
            sockaddr_in6& addr = msg->addrs[0];
            memset(&addr, 0, sizeof(sockaddr_in6));
            addr.sin6_family = AF_INET6;
            memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
            addr.sin6_port = n->ipv6Port;
            
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
            sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
        } else {                    // not broadcast && send to me
            msg->nums_of_addr = 1;
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write_fixed(sqe, tapFd, msg->data, cqe->res, 0, buf_id);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        }
    } else if (config_.runMode == RunMode_Client) {
        if (isSendToMe) {
            msg->nums_of_addr = 1;
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write_fixed(sqe, tapFd, msg->data, cqe->res, 0, buf_id);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        } else {
            udpSockPtr->incDropped(1);
            return 1;
        }
    } else {
        // RunMode_None
    }

    return 0;
}

void TapLan::uring_recv_udp_wrk()
{
    int ret;
    SocketFd udp_fd = static_cast<SocketFd>(*udpSockPtr_);
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    ret = io_uring_queue_init_params(QD, &udp_uring, &params);
    if (ret < 0) {
        LOGF(TAG, "UDP queue init params failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    io_uring_buf_ring *udp_buf_ring;
    size_t buf_ring_size = UDP_BUF_NUM * sizeof(io_uring_buf);
    posix_memalign((void**)(&udp_buf_ring), 4096, buf_ring_size);

    io_uring_buf_reg bufReg{};
    bufReg.ring_addr = reinterpret_cast<uint64_t>(udp_buf_ring);
    bufReg.ring_entries = UDP_BUF_NUM;
    bufReg.bgid = UDP_BUF_GRP_ID;
    ret = io_uring_register_buf_ring(&udp_uring, &bufReg, 0);
    if (ret < 0) {
        LOGF(TAG, "UDP register buf ring failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    recv_bufs = (uint8_t *)mmap(NULL, UDP_BUF_NUM * UDP_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (recv_bufs == MAP_FAILED) {
        posix_memalign((void **)&recv_bufs, 4096, UDP_BUF_NUM * UDP_BUF_SIZE);
    }

    iovec iovs[UDP_BUF_NUM];
    for (size_t i = 0; i < UDP_BUF_NUM; ++i) {
        iovs[i].iov_base = recv_bufs + i * UDP_BUF_SIZE;
        iovs[i].iov_len = UDP_BUF_SIZE;
    }
    ret = io_uring_register_buffers(&udp_uring, iovs, UDP_BUF_NUM);
    if (ret < 0) {
        LOGF(TAG, "UDP register udp buffers failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    io_uring_buf_ring_init(udp_buf_ring);
    int buf_ring_mask = io_uring_buf_ring_mask(UDP_BUF_NUM);
    for (size_t i = 0; i < UDP_BUF_NUM; ++i) {
        io_uring_buf_ring_add(udp_buf_ring, recv_bufs + (i * UDP_BUF_SIZE) + MSG_HDR_SIZE,
                                UDP_BUF_SIZE - MSG_HDR_SIZE, i, buf_ring_mask, i);
    }
    io_uring_buf_ring_advance(udp_buf_ring, UDP_BUF_NUM);

    prep_udp_recv();
    io_uring_submit(&udp_uring);

    auto handle_tap_write = [&](io_uring_cqe *cqe) {
        if (cqe->res < 0) {
            TapDevPtr->incWriteErrs(1);
            LOGE(TAG, "UDP handle tap write failed.[%s]", strerror(-cqe->res));
        }

        TapDevPtr->incWriteBytes(cqe->res);
        uint16_t buf_id = cqe->user_data;
        uring_send_msg *msg = (uring_send_msg *)(recv_bufs + (buf_id * UDP_BUF_SIZE));
        if (!--msg->nums_of_addr)
            return 1;
        return 0;
    };

    auto handle_udp_send = [&](io_uring_cqe *cqe) {
        UdpSocket* udpSockPtr = getUdpSockPtr();
        if (cqe->res < 0) {
            udpSockPtr->incSendErrs(1);
            LOGE(TAG, "UDP handle udp send failed.[%s]", strerror(-cqe->res));
        }

        udpSockPtr->incSendBytes(cqe->res);
        uint16_t buf_id = cqe->user_data;
        uring_send_msg *msg = (uring_send_msg *)(recv_bufs + (buf_id * UDP_BUF_SIZE));
        if (!--msg->nums_of_addr)
            return 1;
        return 0;
    };

    io_uring_cqe *cqe;
    __kernel_timespec timeout{3, 0};
    while (config_.isRunning) {
        ret = io_uring_wait_cqe_timeout(&udp_uring, &cqe, &timeout);
        if (ret < 0 && ret != -ETIME) {
            LOGF(TAG, "UDP wait cqe failed.[%s]", strerror(-ret));
            break;
        }

        unsigned head;
        unsigned cqe_count = 0;
        unsigned ring_count = 0;
        io_uring_for_each_cqe(&udp_uring, head, cqe) {
            cqe_count++;

            unsigned prev_ring_count = ring_count;
            uint16_t buf_id;
            switch (cqe->user_data >> 16)
            {
            case TOKEN_UDP_RECV:
                ring_count += handle_udp_recv(cqe);
                buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                break;

            case TOKEN_TAP_READ:
                break;

            case TOKEN_TAP_WRITE:
                ring_count += handle_tap_write(cqe);
                buf_id = cqe->user_data;
                break;

            case TOKEN_UDP_SEND:
                ring_count += handle_udp_send(cqe);
                buf_id = cqe->user_data;
                break;

            default:
                break;
            }

            if (prev_ring_count != ring_count) {
                io_uring_buf_ring_add(udp_buf_ring, recv_bufs + (buf_id * UDP_BUF_SIZE) + MSG_HDR_SIZE,
                                        UDP_BUF_SIZE - MSG_HDR_SIZE, buf_id, buf_ring_mask, prev_ring_count);
            }
        }

        io_uring_cq_advance(&udp_uring, cqe_count);
        if (ring_count)
            io_uring_buf_ring_advance(udp_buf_ring, ring_count);
        io_uring_submit(&udp_uring);
    }

    io_uring_queue_exit(&udp_uring);
    config_.isRunning = false;
    std::cout << "Thread " << recvThreadName_ << " has exited." << std::endl;
    return ;
}

#endif
