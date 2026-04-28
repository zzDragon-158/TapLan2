#include    "UioIntf.hpp"
#include    "TapLan.hpp"

SioIntf::SioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr)
: UioIntf(tapFd, udpFd, tapLanPtr)
{
    tapIoCtx_.buf = reinterpret_cast<UioCtx::Buf*>(new char [DATA_BUF_SIZE]);
    udpIoCtx_.buf = reinterpret_cast<UioCtx::Buf*>(new char [DATA_BUF_SIZE]);
}

SioIntf::~SioIntf()
{
    delete[] tapIoCtx_.buf;
    delete[] udpIoCtx_.buf;
}

int SioIntf::tapRead(TapFd fd, UioCtx* ctx)
{
    ctx->dataLen = ::read(fd, ctx->buf->payload, PAYLOAD_SIZE);
    if (ctx->dataLen == -1) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            tapPollRead(fd, ctx);
        } else {
            LOGE(TAG, "Failed to read tap.[%s]", strerror(err));
        }
    }

    return ctx->dataLen;
}

int SioIntf::tapPollRead(TapFd fd, UioCtx* ctx)
{
    TapLanPollFd pfd = { fd, POLLIN, 0};
    int res = TapLanPoll(&pfd, 1, IO_WAIT_TIME * 1000);
    if (res == -1) {
        LOGE(TAG, "Failed to poll tap.[%s]", strerror(errno));
    }

    return res;
}

int SioIntf::tapWrite(TapFd fd, UioCtx* ctx)
{
    int res = ::write(fd, ctx->buf->payload, ctx->dataLen);
    if (res == -1) {
        int err = errno;
        LOGE(TAG, "Failed to write tap.[%s]", strerror(err));
    }

    return res;
}

int SioIntf::udpRecv(SockFd fd, UioCtx* ctx)
{
    ctx->addrLen = sizeof(ctx->buf->addr);
    ctx->dataLen = ::recvfrom(fd,
                              ctx->buf->payload,
                              PAYLOAD_SIZE,
                              0,
                              reinterpret_cast<sockaddr*>(&ctx->buf->addr),
                              &ctx->addrLen);
    if (ctx->dataLen == -1) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            udpPollRecv(fd, ctx);
        } else {
            LOGE(TAG, "Failed to recv udp.[%s]", strerror(err));
        }
    }

    return ctx->dataLen;
}

int SioIntf::udpPollRecv(SockFd fd, UioCtx* ctx)
{
    TapLanPollFd pfd = { fd, POLLIN, 0};
    int res = TapLanPoll(&pfd, 1, IO_WAIT_TIME * 1000);
    if (res == -1) {
        LOGE(TAG, "Failed to poll udp.[%s]", strerror(errno));
    }

    return res;
}

int SioIntf::udpSend(SockFd fd, UioCtx* ctx)
{
    int res = ::sendto(fd,
                       ctx->buf->payload,
                       ctx->dataLen,
                       0,
                       reinterpret_cast<sockaddr*>(&ctx->buf->addr),
                       sizeof(ctx->buf->addr));
    if (res == -1) {
        int err = errno;
        LOGE(TAG, "Failed to sendto udp.[%s]", strerror(err));
    }

    return res;
}

AioIntf::~AioIntf()
{
    for (auto ctx: ioCtxs_) delete ctx;
    // FIXME: cant use "free" to release hugepage memory.
    // free(dataBufs_);
}

int AioIntf::initAioIntf()
{
    if (isInitialized) {
        return 0;
    }

    int res;

    ring_ = new io_uring;
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    res = io_uring_queue_init_params(IOURING_SIZE, ring_, &params);
    if (res < 0) {
        LOGF(TAG, "Failed to init queue params.[%s]", strerror(-res));
        g_cfgData.running_ = false;
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
            g_cfgData.running_ = false;
            LOGF(TAG, "Cant allocate [%u] memory.", totalDatBufSize);
            return -1;
        }
    }

    iovec iovs[DATA_BUF_NUM];
    for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
        UioCtx* ctx = new UioCtx();
        ctx->bufId = i;
        ctx->buf = reinterpret_cast<UioCtx::Buf*>(dataBufs_ + i * DATA_BUF_SIZE);
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
    res = io_uring_register_buffers(ring_, iovs, DATA_BUF_NUM);
    if (res < 0) {
        g_cfgData.running_ = false;
        LOGF(TAG, "Failed to register buffers.[%s]", strerror(-res));
        return -1;
    }

    size_t bufRingSize = MAX_RECV_REQ * sizeof(io_uring_buf);
    posix_memalign((void**)(&bufRing_), 4096, bufRingSize);
    if (!bufRing_) {
        g_cfgData.running_ = false;
        LOGF(TAG, "Failed to allocate [%u] memory for io_uring_buf_ring.", bufRingSize);
        return -1;
    }

    io_uring_buf_reg bufReg{};
    bufReg.ring_addr = reinterpret_cast<uint64_t>(bufRing_);
    bufReg.ring_entries = MAX_RECV_REQ;
    bufReg.bgid = 0;
    res = io_uring_register_buf_ring(ring_, &bufReg, 0);
    if (res < 0) {
        LOGF(TAG, "Failed to register buf ring.[%s]", strerror(-res));
        g_cfgData.running_ = false;
        return -1;
    }

    isInitialized = true;
    return 0;
}

int AioIntf::reqTapRead(TapFd fd, UioCtx* ctx)
{
    if (!ctx) {
        ctx = acquireIoCtx();
        if (!ctx) {
            LOGW(TAG, "Failed to acquire aio ctx for reqTapRead.");
            return -1;
        }
    }

    ++ctx->ref;
    ctx->token = TOKEN_TAP_READ;
    ctx->iov.iov_len = PAYLOAD_SIZE;

    io_uring_sqe* sqe = io_uring_get_sqe(ring_);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_readv(sqe, fd, ctx->msgHdr.msg_iov, 
                        ctx->msgHdr.msg_iovlen, 0);

    return 0;
}

int AioIntf::reqTapReadMultishot(TapFd fd)
{
    for (int idx = START_TAP_BUF_IDX; idx < START_TAP_BUF_IDX + MAX_READ_REQ; ++idx) {
        UioCtx* ctx = acquireIoCtx(idx);
        reqTapRead(fd, ctx);
    }

    return 0;
}

int AioIntf::reqTapWrite(TapFd fd, UioCtx* ctx)
{
    ++ctx->ref;
    ctx->token = TOKEN_TAP_WRITE;
    ctx->iov.iov_len = ctx->dataLen;

    io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_writev(sqe, fd, ctx->msgHdr.msg_iov,
                         ctx->msgHdr.msg_iovlen, 0);

    return 0;
}

int AioIntf::reqUdpRecv(SockFd fd, UioCtx* ctx)
{
    if (!ctx) {
        ctx = acquireIoCtx();
        if (!ctx) {
            LOGW(TAG, "Failed to acquire aio ctx for reqUdpRecv.");
            return -1;
        }
    }

    ++ctx->ref;
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

        io_uring_sqe* sqe = io_uring_get_sqe(ring_);
        sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
        io_uring_prep_recvmsg(sqe, fd, &ctx->msgHdr, 0);
    }

    return 0;
}

int AioIntf::reqUdpRecvMultishot(SockFd fd)
{
    io_uring_buf_ring_init(bufRing_);
    bufRingMask_ = io_uring_buf_ring_mask(MAX_RECV_REQ);
    for (size_t i = 0; i < MAX_RECV_REQ; ++i) {
        UioCtx* ctx = acquireIoCtx(i);
        reqUdpRecv(fd, ctx);
    }

    UioCtx* ctx = acquireIoCtx();
    ++ctx->ref;
    ctx->token = TOKEN_UDP_RECV_MULTISHOT;
    memset(&ctx->msgHdr, 0, sizeof(msghdr));
    ctx->msgHdr.msg_namelen = sizeof(ctx->buf->addr);

    io_uring_sqe* sqe = io_uring_get_sqe(ring_);
    io_uring_prep_recvmsg_multishot(sqe, fd, &ctx->msgHdr, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = 0;
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);

    return 0;
}

int AioIntf::reqUdpSend(SockFd fd, UioCtx* ctx)
{
    ++ctx->ref;
    ctx->token = TOKEN_UDP_SEND;
    ctx->msgHdr.msg_namelen = sizeof(sockaddr_in6);
    ctx->iov.iov_len = ctx->dataLen;

    io_uring_sqe* sqe = io_uring_get_sqe(ring_);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_sendmsg(sqe, fd, &ctx->msgHdr, 0);

    return 0;
}

void AioIntf::aioWrk()
{
    if (initAioIntf() != 0) {
        return ;
    }

    reqTapReadMultishot(tapFd_);
    reqUdpRecvMultishot(udpFd_);
    io_uring_submit(ring_);

    io_uring_cqe *cqe;
    __kernel_timespec timeout{IO_WAIT_TIME, 0};
    constexpr size_t maxCqeBatch = IOURING_SIZE / 2;
    while (g_cfgData.running_) {
        int res = io_uring_wait_cqe_timeout(ring_, &cqe, &timeout);
        if (res < 0 && res != -ETIME) {
            LOGF(TAG, "Failed to wait cqe.[%s]", strerror(-res));
            break;
        }

        unsigned head;
        unsigned cqeCnt = 0;
        io_uring_for_each_cqe(ring_, head, cqe) {
            ++cqeCnt;

            UioCtx* ctx = reinterpret_cast<UioCtx*>(cqe->user_data);
            ctx->dataLen = cqe->res;

            switch (ctx->token) {
            case TOKEN_UDP_RECV_MULTISHOT:
                if (!(cqe->flags & IORING_CQE_F_MORE)) {
                    LOGE(TAG, "UDP recvmsg multishot stop.[%d]", strerror(-cqe->res));
                    reqUdpRecvMultishot(udpFd_);
                    break;
                }

                ctx = acquireIoCtx(cqe->flags >> IORING_CQE_BUFFER_SHIFT);
                ctx->dataLen = cqe->res;
                handleUdpRecv(ctx);
                break;

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
                io_uring_cq_advance(ring_, cqeCnt);
                io_uring_submit(ring_);
                cqeCnt = 0;
            }
        }

        io_uring_cq_advance(ring_, cqeCnt);
        io_uring_submit(ring_);
    }

    LOGI(TAG, "aioWrk has exited.");
}
