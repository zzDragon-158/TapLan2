#include    "AioIntf.hpp"

const size_t PAYLOAD_SIZE = DATA_BUF_SIZE - sizeof(AioIntf::Buf);
/* 0               256             512             768             1024 */
/* |---------------|---------------|---------------|---------------|    */
/* |      udp      |      tap      |           freestack           |    */
const size_t START_UDP_BUF_IDX = 0;
const size_t START_TAP_BUF_IDX = MAX_RECV_REQ;
const size_t UDP_MULTISHOT_BUF_IDX = 512;
const size_t START_FREE_BUF_IDX = 513;
static const char* TAG = "[AioIntf]";

AioIntf::AioIntf()
{
    int ret;

    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    ret = io_uring_queue_init_params(IOURING_SIZE, &ring_, &params);
    if (ret < 0) {
        LOGF(TAG, "Failed to init queue params.[%s]", strerror(-ret));
        g_cfgData.isRunning = false;
        return ;
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
            return ;
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
    ret = io_uring_register_buffers(&ring_, iovs, DATA_BUF_NUM);
    if (ret < 0) {
        g_cfgData.isRunning = false;
        LOGF(TAG, "Failed to register buffers.[%s]", strerror(-ret));
        return ;
    }

    size_t bufRingSize = MAX_RECV_REQ * sizeof(io_uring_buf);
    posix_memalign((void**)(&bufRing_), 4096, bufRingSize);
    if (!dataBufs_) {
        g_cfgData.isRunning = false;
        LOGF(TAG, "Failed to allocate [%u] memory for io_uring_buf_ring.", bufRingSize);
        return ;
    }

    io_uring_buf_reg bufReg{};
    bufReg.ring_addr = reinterpret_cast<uint64_t>(bufRing_);
    bufReg.ring_entries = MAX_RECV_REQ;
    bufReg.bgid = 0;
    ret = io_uring_register_buf_ring(&ring_, &bufReg, 0);
    if (ret < 0) {
        LOGF(TAG, "Failed to register buf ring.[%s]", strerror(-ret));
        g_cfgData.isRunning = false;
        return ;
    }

    io_uring_buf_ring_init(bufRing_);
    bufRingMask_ = io_uring_buf_ring_mask(MAX_RECV_REQ);
    for (size_t i = 0; i < MAX_RECV_REQ; ++i) {
        Ctx* ctx = acquireAioCtx(i);
        ctx->token = TOKEN_UDP_RECV_MULTISHOT;
        io_uring_buf_ring_add(bufRing_, ctx->buf, DATA_BUF_SIZE,
                              ctx->bufId, bufRingMask_, i);
    }
    io_uring_buf_ring_advance(bufRing_, MAX_RECV_REQ);
}

AioIntf::~AioIntf()
{
    for (auto ctx: ioCtxs_) delete ctx;
    // FIXME: cant use "free" to release hugepage memory.
    // free(dataBufs_);
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
    static unsigned short bufCnt = 0;
    if (ctx->bufId < START_TAP_BUF_IDX) {
        ctx->token = TOKEN_UDP_RECV_MULTISHOT;
        io_uring_buf_ring_add(bufRing_, ctx->buf, DATA_BUF_SIZE,
                              ctx->bufId, bufRingMask_, bufCnt++);
    } else if (ctx->bufId < UDP_MULTISHOT_BUF_IDX) {
        reqTapRead(tapFd, ctx);
    } else
        freeStack_.push(ctx);

    if (bufCnt >= MAX_RECV_REQ / 2) {
        LOGT(TAG, "buf ring advance [%u].", bufCnt);
        io_uring_buf_ring_advance(bufRing_, bufCnt);
        bufCnt = 0;
    }
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

int AioIntf::reqTapReadMultishot(SocketFd fd)
{
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

int AioIntf::reqUdpRecv(SocketFd fd, Ctx* ctx)
{
    if (!ctx) {
        ctx = acquireAioCtx();
        if (!ctx) {
            LOGW(TAG, "Failed to acquire aio ctx for reqUdpRecv.");
            return -1;
        }
    }

    ctx->token = TOKEN_UDP_RECV;
    ctx->msgHdr.msg_namelen = sizeof(ctx->buf->addr);
    ctx->iov.iov_len = PAYLOAD_SIZE;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_recvmsg(sqe, fd, &ctx->msgHdr, 0);

    return 0;
}

int AioIntf::reqUdpRecvMultishot(SocketFd fd) {
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

int AioIntf::reqUdpSend(SocketFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_UDP_SEND;
    ctx->msgHdr.msg_namelen = sizeof(sockaddr_in6);
    ctx->iov.iov_len = ctx->bufLen;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_sendmsg(sqe, fd, &ctx->msgHdr, 0);

    return 0;
}
