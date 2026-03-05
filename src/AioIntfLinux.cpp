#include    "AioIntf.hpp"

const size_t IOURING_SIZE = 512;
static const char* TAG = "[AioIntf]";

AioIntf::AioIntf() {
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
            return;
        }
    }

    // iovec iovs[DATA_BUF_NUM];
    for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
        Ctx* ctx = new Ctx();

        ctx->owner = this;
        ctx->ring = &ring_;
        // ctx->bufId = i;
        ctx->buf = dataBufs_ + i * DATA_BUF_SIZE;
        ctx->msgHdr.msg_name = &ctx->addr;
        ctx->msgHdr.msg_namelen = sizeof(ctx->addr);
        ctx->msgHdr.msg_iov = &ctx->iov;
        ctx->msgHdr.msg_iovlen = 1;
        ctx->iov.iov_base = ctx->buf;
        ctx->iov.iov_len = DATA_BUF_SIZE;

        // iovs[i].iov_base = ctx->buf;
        // iovs[i].iov_len = DATA_BUF_SIZE;

        ioCtxs_.push_back(ctx);
        freeStack_.push(ctx);
    }
    // ret = io_uring_register_buffers(&ring_, iovs, DATA_BUF_NUM);
    // if (ret < 0) {
    //     LOGF(TAG, "Failed to register buffers.[%s]", strerror(-ret));
    //     g_cfgData.isRunning = false;
    // }
    io_uring_submit(&ring_);
}

AioIntf::~AioIntf() {
    for (auto ctx: ioCtxs_) delete ctx;
    // FIXME: cant use "free" to release hugepage memory.
    // free(dataBufs_);
}

AioIntf::Ctx* AioIntf::acquireAioCtx() {
    if (freeStack_.empty())
        return nullptr;

    Ctx* ctx = freeStack_.top();
    freeStack_.pop();
    return ctx;
}

void AioIntf::releaseAioCtx(Ctx* ctx) {
    freeStack_.push(ctx);
}

int AioIntf::reqTapRead(TapFd fd)
{
    Ctx* ctx = acquireAioCtx();
    ctx->token = TOKEN_TAP_READ;
    ctx->iov.iov_len = DATA_BUF_SIZE;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_read(sqe, tapFd, ctx->buf, DATA_BUF_SIZE, 0);

    return 0;
}

int AioIntf::reqTapWrite(TapFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_TAP_WRITE;

    io_uring_sqe *sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_write(sqe, tapFd, ctx->buf, ctx->bufLen, 0);

    return 0;
}

int AioIntf::reqUdpRecv(SocketFd fd)
{
    Ctx* ctx = acquireAioCtx();
    ctx->token = TOKEN_UDP_RECV;
    ctx->msgHdr.msg_namelen = sizeof(ctx->addr);
    ctx->iov.iov_len = DATA_BUF_SIZE;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_recvmsg(sqe, fd, &ctx->msgHdr, 0);

    return 0;
}

int AioIntf::reqUdpSend(SocketFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_UDP_SEND;
    ctx->msgHdr.msg_namelen = sizeof(ctx->addr);
    ctx->iov.iov_len = ctx->bufLen;

    io_uring_sqe* sqe = io_uring_get_sqe(ctx->ring);
    sqe->user_data = reinterpret_cast<unsigned long long>(ctx);
    io_uring_prep_sendmsg(sqe, fd, &ctx->msgHdr, 0);

    return 0;
}
