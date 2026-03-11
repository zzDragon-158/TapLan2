#include    "AioIntf.hpp"

static const char* TAG = "[AioIntf]";
const size_t PAYLOAD_SIZE = DATA_BUF_SIZE - sizeof(AioIntf::Buf);
/* 0               256             512             768             1024 */
/* |---------------|---------------|---------------|---------------|    */
/* |      udp      |      tap      |           freestack           |    */
const size_t START_UDP_BUF_IDX = 0;
const size_t START_TAP_BUF_IDX = MAX_RECV_REQ;
const size_t START_FREE_BUF_IDX = MAX_READ_REQ + MAX_RECV_REQ;

AioIntf::AioIntf()
{
    std::string errMsg;

    size_t totalDatBufSize = DATA_BUF_NUM * DATA_BUF_SIZE;
    dataBufs_ = (uint8_t*)_aligned_malloc(totalDatBufSize, 64);
    if (dataBufs_ == nullptr) {
        g_cfgData.isRunning = false;
        LOGF(TAG, "Cant allocate [%u] memory.", totalDatBufSize);
        return;
    }

    for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
        Ctx* ctx = new Ctx();

        ctx->owner = this;
        ctx->bufId = i;
        ctx->buf = reinterpret_cast<Buf*>(dataBufs_ + i * DATA_BUF_SIZE);
        ZeroMemory(ctx->buf, sizeof(Buf));
        ctx->wsaBuf.buf = ctx->buf->payload;

        ioCtxs_.push_back(ctx);
        if (i > START_FREE_BUF_IDX)
            freeStack_.push(ctx);
    }

    hIOCP_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (hIOCP_ == nullptr) {
        g_cfgData.isRunning = false;
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to create IOCP.[%s]", errMsg.c_str());
    }
}

AioIntf::~AioIntf()
{
    for (auto ctx: ioCtxs_) delete ctx;
    _aligned_free(dataBufs_);
}

AioIntf::Ctx* AioIntf::acquireAioCtx() {
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
    } else if (ctx->bufId < START_FREE_BUF_IDX) {
        reqTapRead(tapFd_, ctx);
    } else {
        freeStack_.push(ctx);
    }
}

int AioIntf::reqTapRead(TapFd fd, Ctx* ctx)
{
    if (!ctx)
        ctx = acquireAioCtx();

    ctx->token = TOKEN_TAP_READ;
    ZeroMemory(&ctx->buf->ol, sizeof(OVERLAPPED));

    BOOL ok = ReadFile(fd, ctx->buf->payload, PAYLOAD_SIZE, nullptr, &ctx->buf->ol);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);

            LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
            return -1;
        }
        ctx->isPending = true;
    } else {
        ctx->isPending = false;
        LOGD(TAG, "fuck sio[%p].", ctx);
    }

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
    ZeroMemory(&ctx->buf->ol, sizeof(OVERLAPPED));

    BOOL ok = WriteFile(fd, ctx->buf->payload, ctx->bufLen, nullptr, &ctx->buf->ol);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);

            LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
            return -1;
        }
        ctx->isPending = true;
    } else {
        ctx->isPending = false;
        LOGD(TAG, "fuck sio[%p].", ctx);
    }

    return 0;
}

int AioIntf::reqUdpRecv(SocketFd fd, Ctx* ctx)
{
    if (!ctx)
        ctx = acquireAioCtx();

    ctx->token = TOKEN_UDP_RECV;
    ctx->buf->addrLen = sizeof(ctx->buf->addr);
    ZeroMemory(&ctx->buf->ol, sizeof(OVERLAPPED));
    ctx->wsaBuf.len = PAYLOAD_SIZE;

    DWORD flags = 0;
    int ret = WSARecvFrom(
        fd,
        &ctx->wsaBuf,
        1,
        nullptr,
        &flags,
        reinterpret_cast<sockaddr *>(&ctx->buf->addr),
        &ctx->buf->addrLen,
        &ctx->buf->ol,
        nullptr
    );
    if (ret == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);
            LOGE(TAG, "Failed to recv udp.[%s]", getErrMsg(err).c_str());
        }
        ctx->isPending = true;
    } else {
        ctx->isPending = false;
        LOGD(TAG, "fuck sio[%p].", ctx);
    }

    return ret;
}

int AioIntf::reqUdpRecvMultishot(SocketFd fd)
{
    udpFd_ = fd;

    for (int idx = START_UDP_BUF_IDX; idx < START_UDP_BUF_IDX + MAX_RECV_REQ; ++idx) {
        Ctx* ctx = acquireAioCtx(idx);
        reqUdpRecv(fd, ctx);
    }

    return 0;
}

int AioIntf::reqUdpSend(SocketFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_UDP_SEND;
    ZeroMemory(&ctx->buf->ol, sizeof(OVERLAPPED));
    ctx->wsaBuf.len = ctx->bufLen;

    int ret = WSASendTo(
        fd,
        &ctx->wsaBuf,
        1,
        nullptr,
        0,
        (const sockaddr*)&ctx->buf->addr, 
        sizeof(sockaddr_in6),
        &ctx->buf->ol,
        nullptr
    );

    if (ret == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);
            LOGE(TAG, "Failed to send udp.[%s]", getErrMsg(err).c_str());
        }
        ctx->isPending = true;
    } else {
        ctx->isPending = false;
        LOGD(TAG, "fuck sio[%p].", ctx);
    }

    return ret;
}
