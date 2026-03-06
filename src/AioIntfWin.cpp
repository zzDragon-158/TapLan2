#include    "AioIntf.hpp"

static const char* TAG = "[AioIntf]";

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
        ctx->buf = dataBufs_ + i * DATA_BUF_SIZE;

        ioCtxs_.push_back(ctx);
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

void AioIntf::releaseAioCtx(Ctx* ctx) {
    freeStack_.push(ctx);
}

int AioIntf::reqTapRead(TapFd fd, Ctx* ctx)
{
    if (!ctx)
        ctx = acquireAioCtx();

    ctx->token = TOKEN_TAP_READ;
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));

    BOOL ok = ReadFile(fd, ctx->buf, DATA_BUF_SIZE, nullptr, &ctx->overlapped);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);
            LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
        }

        return -1;
    }

    return 0;
}

int AioIntf::reqTapWrite(TapFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_TAP_WRITE;
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));

    BOOL ok = WriteFile(fd, ctx->buf, DATA_BUF_SIZE, nullptr, &ctx->overlapped);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
            ctx->owner->releaseAioCtx(ctx);
            return -1;
        }
    }

    return 0;
}

int AioIntf::reqUdpRecv(SocketFd fd, Ctx* ctx)
{
    if (!ctx)
        ctx = acquireAioCtx();

    ctx->token = TOKEN_UDP_RECV;
    ctx->addrLen = sizeof(sockaddr_in6);
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));

    WSABUF wsaBuf;
    wsaBuf.buf = (char*)ctx->buf;
    wsaBuf.len = DATA_BUF_SIZE;

    DWORD flags = 0;
    int ret = WSARecvFrom(
        fd,
        &wsaBuf,
        1,
        nullptr,
        &flags,
        reinterpret_cast<sockaddr *>(&ctx->addr),
        &ctx->addrLen,
        &ctx->overlapped,
        nullptr
    );
    if (ret == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);
            LOGE(TAG, "Failed to recv udp.[%s]", getErrMsg(err).c_str());
        }
    }

    return ret;
}

int AioIntf::reqUdpSend(SocketFd fd, Ctx* ctx)
{
    ctx->token = TOKEN_UDP_SEND;
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));

    WSABUF wsaBuf;
    wsaBuf.buf = (char*)ctx->buf;
    wsaBuf.len = (ULONG)ctx->bufLen;

    int ret = WSASendTo(
        fd,
        &wsaBuf, 
        1, 
        nullptr, 
        0, 
        (const sockaddr*)&ctx->addr, 
        sizeof(sockaddr_in6), 
        &ctx->overlapped, 
        nullptr
    );

    if (ret == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            ctx->owner->releaseAioCtx(ctx);
            LOGE(TAG, "Failed to send udp.[%s]", getErrMsg(err).c_str());
        }
    }

    return ret;
}
