#include    "AioIntf.hpp"

static const char* TAG = "[AioIntf]";
const size_t PAYLOAD_SIZE = DATA_BUF_SIZE - sizeof(AioIntf::Buf);
/* 0               256             512             768             1024 */
/* |---------------|---------------|---------------|---------------|    */
/* |      udp      |      tap      |           freestack           |    */
const size_t START_UDP_BUF_IDX = 0;
const size_t START_TAP_BUF_IDX = MAX_RECV_REQ;
const size_t UDP_MULTISHOT_BUF_IDX = MAX_READ_REQ + MAX_RECV_REQ;
const size_t START_FREE_BUF_IDX = UDP_MULTISHOT_BUF_IDX + 1;

AioIntf::AioIntf()
{
    ;
}

AioIntf::~AioIntf()
{
    for (auto ctx: ioCtxs_) delete ctx;
    // FIXME: cant use "free" to release hugepage memory.
    // free(dataBufs_);
}

int AioIntf::initAioIntf()
{
    int res;

    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    res = io_uring_queue_init_params(IOURING_SIZE, &ring_, &params);
    if (res < 0) {
        LOGF(TAG, "Failed to init queue params.[%s]", strerror(-res));
        g_cfgData.isRunning = false;
        return -1;
    }

    size_t totalDatBufSize = DATA_BUF_NUM * DATA_BUF_SIZE;
    dataBufs_ = (uint8_t*)mmap(NULL, totalDatBufSize,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                               -1, 0);
    if (dataBufs_ == MAP_FAILED) {
        LOGD(TAG, "Cant allocate [%u] hugepage memory.", totalDatBufSize);
        posix_memalign((void **)&dataBufs_, 4096, totalDatBufSize);
        if (!dataBufs_) {
            g_cfgData.isRunning = false;
            LOGF(TAG, "Cant allocate [%u] memory.", totalDatBufSize);
            return -1;
        }
    }

    iovec iovs[DATA_BUF_NUM];
    for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
        Ctx* ctx = new Ctx();
        ctx->owner = this;
        ctx->ring = &ring_;
        ctx->bufId = i;
        ctx->buf = reinterpret_cast<Buf*>(dataBufs_ + i * DATA_BUF_SIZE);
        ctx->msgHdr.msg_name = &ctx->buf->addr;
        ctx->msgHdr.msg_namelen = sizeof(ctx->buf->addr);
        ctx->msgHdr.msg_iov = &ctx->iov;
        ctx->msgHdr.msg_iovlen = 1;
        ctx->iov.iov_base = ctx->buf->payload;

        iovs[i].iov_base = ctx->buf;
        iovs[i].iov_len = DATA_BUF_SIZE;

        ioCtxs_.push_back(ctx);
        if (i > START_FREE_BUF_IDX)
            freeStack_.push(ctx);
    }
    res = io_uring_register_buffers(&ring_, iovs, DATA_BUF_NUM);
    if (res < 0) {
        g_cfgData.isRunning = false;
        LOGF(TAG, "Failed to register buffers.[%s]", strerror(-res));
        return -1;
    }

    size_t bufRingSize = MAX_RECV_REQ * sizeof(io_uring_buf);
    posix_memalign((void**)(&bufRing_), 4096, bufRingSize);
    if (!bufRing_) {
        g_cfgData.isRunning = false;
        LOGF(TAG, "Failed to allocate [%u] memory for io_uring_buf_ring.", bufRingSize);
        return -1;
    }

    io_uring_buf_reg bufReg{};
    bufReg.ring_addr = reinterpret_cast<uint64_t>(bufRing_);
    bufReg.ring_entries = MAX_RECV_REQ;
    bufReg.bgid = 0;
    res = io_uring_register_buf_ring(&ring_, &bufReg, 0);
    if (res < 0) {
        LOGF(TAG, "Failed to register buf ring.[%s]", strerror(-res));
        g_cfgData.isRunning = false;
        return -1;
    }

    return 0;
}

AioIntf::Ctx* AioIntf::acquireAioCtx()
{
    if (freeStack_.empty())
        return nullptr;

    Ctx* ctx = freeStack_.top();
    freeStack_.pop();
    return ctx;
}

AioIntf::Ctx* AioIntf::acquireAioCtx(size_t idx)
{
    if (idx >= DATA_BUF_NUM)
        return nullptr;

    return ioCtxs_[idx];
}

void AioIntf::releaseAioCtx(Ctx* ctx)
{
    if (ctx->bufId < START_TAP_BUF_IDX) {
        reqUdpRecv(udpFd_, ctx);
    } else if (ctx->bufId < UDP_MULTISHOT_BUF_IDX) {
        reqTapRead(tapFd_, ctx);
    } else
        freeStack_.push(ctx);
}

int AioIntf::reqTapRead(TapFd fd, Ctx* ctx)
{
    if (!ctx)
        ctx = acquireAioCtx();

    if (!ctx) {
        LOGW(TAG, "Failed to acquire aio ctx for reqTapRead.");
        return -1;
    }

    ctx->token = TOKEN_TAP_READ;
    ctx->iov.iov_len = PAYLOAD_SIZE;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_readv(sqe, fd, ctx->msgHdr.msg_iov, 
                        ctx->msgHdr.msg_iovlen, 0);

    return 0;
}

int AioIntf::reqTapReadMultishot(TapFd fd)
{
    tapFd_ = fd;

    for (int idx = START_TAP_BUF_IDX; idx < START_TAP_BUF_IDX + MAX_READ_REQ; ++idx) {
        Ctx* ctx = acquireAioCtx(idx);
        reqTapRead(fd, ctx);
    }

    return 0;
}

int AioIntf::reqTapWrite(TapFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_TAP_WRITE;
    ctx->iov.iov_len = ctx->bufLen;

    io_uring_sqe *sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_writev(sqe, fd, ctx->msgHdr.msg_iov,
                         ctx->msgHdr.msg_iovlen, 0);

    return 0;
}

int AioIntf::reqUdpRecv(SockFd fd, Ctx* ctx)
{
    if (!ctx) {
        ctx = acquireAioCtx();
        if (!ctx) {
            LOGW(TAG, "Failed to acquire aio ctx for reqUdpRecv.");
            return -1;
        }
    }

    if (ctx->bufId < START_TAP_BUF_IDX) {
        ctx->token = TOKEN_UDP_RECV_MULTISHOT;
        io_uring_buf_ring_add(bufRing_, ctx->buf, DATA_BUF_SIZE,
                              ctx->bufId, bufRingMask_, advanceCnt_++);
        if (advanceCnt_ >= MAX_RECV_REQ / 2) {
            LOGT(TAG, "buf ring advance [%u].", advanceCnt_);
            io_uring_buf_ring_advance(bufRing_, advanceCnt_);
            advanceCnt_ = 0;
        }
    } else {
        ctx->token = TOKEN_UDP_RECV;
        ctx->msgHdr.msg_namelen = sizeof(ctx->buf->addr);
        ctx->iov.iov_len = PAYLOAD_SIZE;

        io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
        sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
        io_uring_prep_recvmsg(sqe, fd, &ctx->msgHdr, 0);
    }

    return 0;
}

int AioIntf::reqUdpRecvMultishot(SockFd fd) {
    udpFd_ = fd;

    io_uring_buf_ring_init(bufRing_);
    bufRingMask_ = io_uring_buf_ring_mask(MAX_RECV_REQ);
    for (size_t i = 0; i < MAX_RECV_REQ; ++i) {
        Ctx* ctx = acquireAioCtx(i);
        reqUdpRecv(fd, ctx);
    }

    Ctx* ctx = acquireAioCtx(UDP_MULTISHOT_BUF_IDX);
    ctx->token = TOKEN_UDP_RECV_MULTISHOT;
    memset(&ctx->msgHdr, 0, sizeof(msghdr));
    ctx->msgHdr.msg_namelen = sizeof(ctx->buf->addr);

    io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_recvmsg_multishot(sqe, fd, &ctx->msgHdr, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = 0;
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);

    return 0;
}

int AioIntf::reqUdpSend(SockFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_UDP_SEND;
    ctx->msgHdr.msg_namelen = sizeof(sockaddr_in6);
    ctx->iov.iov_len = ctx->bufLen;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_sendmsg(sqe, fd, &ctx->msgHdr, 0);

    return 0;
}

int AioIntf::handleTapRead(Ctx* ctx)
{
    // SockFd udpSendFd = static_cast<SockFd>(*getUdpSockPtr());
    // sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
    // char* payload = ctx->buf->payload;

    // EthHdr& eh = reinterpret_cast<EthHdr&>(*payload);
    // Mac& dstMac = eh.dst;
    // Mac& srcMac = eh.src;
    // bool needBroadcast = eh.dst[0] & 0x01;

    // if (g_cfgData.runMode == RunMode_Server) {
    //     if (!needBroadcast) {
    //         auto n = nodeMgrPtr_->findNode(dstMac);
    //         if (n) {
    //             dstAddr.sin6_family = AF_INET6;
    //             memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
    //             dstAddr.sin6_port = n->ipv6Port;

    //             AioIntfPtr->reqUdpSend(udpSendFd, ctx);
    //             return;
    //         }
    //     } else {
    //         nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
    //             if (n->status == NODE_OFFLINE || n->mac == srcMac)
    //                 return;

    //             AioIntf::Ctx* sendCtx = AioIntfPtr->acquireAioCtx();
    //             if (sendCtx) {
    //                 sockaddr_in6& sendAddr = reinterpret_cast<sockaddr_in6&>(sendCtx->buf->addr);
    //                 memcpy(sendCtx->buf->payload, payload, ctx->bufLen);
    //                 sendCtx->bufLen = ctx->bufLen;

    //                 nodeMgrPtr_->setSockaddr(sendAddr, n);

    //                 AioIntfPtr->reqUdpSend(udpSendFd, sendCtx);
    //             }
    //         });
    //     }
    // } else if (g_cfgData.runMode == RunMode_Client) {
    //     if (!g_cfgData.noSync || nodeMgrPtr_->findNode(dstMac) || needBroadcast) {
    //         memcpy(&dstAddr, &serverAddr_, sizeof(sockaddr_in6));

    //         AioIntfPtr->reqUdpSend(udpSendFd, ctx);
    //         return;
    //     }
    // }
    return 0;
}

int AioIntf::handleTapWrite(Ctx* ctx)
{
    return 0;
}

int AioIntf::handleUdpRecv(Ctx* ctx)
{
    // TapFd tapFd = TapDevPtr->getFd();
    // SockFd udpSendFd = static_cast<SockFd>(*getUdpSockPtr());
    // sockaddr_in6& srcAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
    // char* payload = ctx->buf->payload;

    // EthHdr eh = reinterpret_cast<EthHdr&>(*payload);
    // Mac& dstMac = eh.dst;
    // Mac& srcMac = eh.src;
    // bool needBroadcast = eh.dst[0] & 0x01;
    // bool isSendToMe = needBroadcast || (dstMac == g_cfgData.mac);

    // if (g_cfgData.noSync) {
    //     nodeMgrPtr_->addNode(&srcAddr, srcMac);
    // }

    // if (g_cfgData.runMode == RunMode_Server) {
    //     if (needBroadcast) {
    //         nodeMgrPtr_->forEach([&](uint64_t m, NodeSPtr n) {
    //             if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == g_cfgData.mac)
    //                 return;

    //             AioIntf::Ctx* sendCtx = AioIntfPtr->acquireAioCtx();
    //             if (sendCtx) {
    //                 sockaddr_in6& sendAddr = reinterpret_cast<sockaddr_in6&>(sendCtx->buf->addr);
    //                 memcpy(sendCtx->buf->payload, payload, ctx->bufLen);
    //                 sendCtx->bufLen = ctx->bufLen;

    //                 nodeMgrPtr_->setSockaddr(sendAddr, n);

    //                 AioIntfPtr->reqUdpSend(udpSendFd, sendCtx);
    //             }
    //         });

    //         AioIntfPtr->reqTapWrite(tapFd, ctx);
    //         return;

    //     } else if (!isSendToMe) {
    //         auto n = nodeMgrPtr_->findNode(dstMac);
    //         if (n) {
    //             sockaddr_in6& dstAddr = reinterpret_cast<sockaddr_in6&>(ctx->buf->addr);
    //             nodeMgrPtr_->setSockaddr(dstAddr, n);

    //             AioIntfPtr->reqUdpSend(udpSendFd, ctx);
    //             return;
    //         }
    //     } else {
    //         AioIntfPtr->reqTapWrite(tapFd, ctx);
    //         return;
    //     }
    // } else if (g_cfgData.runMode == RunMode_Client) {
    //     AioIntfPtr->reqTapWrite(tapFd, ctx);
    //     return;
    // }
    return 0;
}

int AioIntf::handleUdpSend(Ctx* ctx)
{
    return 0;
}
