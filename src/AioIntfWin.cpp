#include    "AioIntf.hpp"
#include    "TapLan.hpp"

static const char* TAG = "[AioIntf]";
const size_t PAYLOAD_SIZE = DATA_BUF_SIZE - sizeof(IoCtx::Buf);
/* 0               256             512             768             1024 */
/* |---------------|---------------|---------------|---------------|    */
/* |      udp      |      tap      |           freestack           |    */
const size_t START_UDP_BUF_IDX = 0;
const size_t START_TAP_BUF_IDX = MAX_RECV_REQ;
const size_t START_FREE_BUF_IDX = MAX_READ_REQ + MAX_RECV_REQ;

AioIntf::AioIntf()
{
    ;
}

AioIntf::~AioIntf()
{
    for (auto ctx: ioCtxs_) delete ctx;
    _aligned_free(dataBufs_);
}

int AioIntf::initAioIntf()
{
    std::string errMsg;

    size_t totalDatBufSize = DATA_BUF_NUM * DATA_BUF_SIZE;
    dataBufs_ = (uint8_t*)_aligned_malloc(totalDatBufSize, 64);
    if (dataBufs_ == nullptr) {
        g_cfgData.isRunning = false;
        LOGF(TAG, "Cant allocate [%u] memory.", totalDatBufSize);
        return -1;
    }

    for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
        IoCtx* ctx = new IoCtx();

        ctx->bufId = i;
        ctx->buf = reinterpret_cast<IoCtx::Buf*>(dataBufs_ + i * DATA_BUF_SIZE);
        ZeroMemory(ctx->buf, sizeof(IoCtx::Buf));
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
        return -1;
    }

    return 0;
}

IoCtx* AioIntf::acquireIoCtx() {
    if (freeStack_.empty())
        return nullptr;

    IoCtx* ctx = freeStack_.top();
    freeStack_.pop();
    return ctx;
}

IoCtx* AioIntf::acquireIoCtx(size_t idx)
{
    if (idx >= DATA_BUF_NUM)
        return nullptr;

    return ioCtxs_[idx];
}

void AioIntf::releaseIoCtx(IoCtx* ctx)
{
    --ctx->ref;
    if (ctx->ref > 0)
        return ;

    if (ctx->bufId < START_TAP_BUF_IDX) {
        reqUdpRecv(udpFd_, ctx);
    } else if (ctx->bufId < START_FREE_BUF_IDX) {
        reqTapRead(tapFd_, ctx);
    } else {
        freeStack_.push(ctx);
    }
}

int AioIntf::reqTapRead(TapFd fd, IoCtx* ctx)
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
    ZeroMemory(&ctx->ol, sizeof(OVERLAPPED));

    BOOL ok = ReadFile(fd, ctx->buf->payload, PAYLOAD_SIZE, nullptr, &ctx->ol);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
            releaseIoCtx(ctx);
            return -1;
        }
    }

    return 0;
}

int AioIntf::reqTapReadMultishot(TapFd fd)
{
    tapFd_ = fd;

    for (int idx = START_TAP_BUF_IDX; idx < START_TAP_BUF_IDX + MAX_READ_REQ; ++idx) {
        IoCtx* ctx = acquireIoCtx(idx);
        reqTapRead(fd, ctx);
    }

    return 0;
}

int AioIntf::reqTapWrite(TapFd fd, IoCtx* ctx)
{
    ++ctx->ref;
    ctx->token = TOKEN_TAP_WRITE;
    ZeroMemory(&ctx->ol, sizeof(OVERLAPPED));

    BOOL ok = WriteFile(fd, ctx->buf->payload, ctx->dataLen, nullptr, &ctx->ol);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
            releaseIoCtx(ctx);
            return -1;
        }
    }

    return 0;
}

int AioIntf::reqUdpRecv(SockFd fd, IoCtx* ctx)
{
    if (!ctx) {
        ctx = acquireIoCtx();
        if (!ctx) {
            LOGW(TAG, "Failed to acquire aio ctx for reqUdpRecv.");
            return -1;
        }
    }

    ++ctx->ref;
    ctx->token = TOKEN_UDP_RECV;
    ctx->buf->addrLen = sizeof(ctx->buf->addr);
    ZeroMemory(&ctx->ol, sizeof(OVERLAPPED));
    ctx->wsaBuf.len = PAYLOAD_SIZE;

    DWORD flags = 0;
    int res = WSARecvFrom(
        fd,
        &ctx->wsaBuf,
        1,
        nullptr,
        &flags,
        reinterpret_cast<sockaddr *>(&ctx->buf->addr),
        &ctx->buf->addrLen,
        &ctx->ol,
        nullptr
    );
    if (res == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOGE(TAG, "Failed to recv udp.[%s]", getErrMsg(err).c_str());
            releaseIoCtx(ctx);
            return -1;
        }
    }

    return 0;
}

int AioIntf::reqUdpRecvMultishot(SockFd fd)
{
    udpFd_ = fd;

    for (int idx = START_UDP_BUF_IDX; idx < START_UDP_BUF_IDX + MAX_RECV_REQ; ++idx) {
        IoCtx* ctx = acquireIoCtx(idx);
        reqUdpRecv(fd, ctx);
    }

    return 0;
}

int AioIntf::reqUdpSend(SockFd fd, IoCtx* ctx)
{
    ++ctx->ref;
    ctx->token = TOKEN_UDP_SEND;
    ZeroMemory(&ctx->ol, sizeof(OVERLAPPED));
    ctx->wsaBuf.len = ctx->dataLen;

    int res = WSASendTo(
        fd,
        &ctx->wsaBuf,
        1,
        nullptr,
        0,
        (const sockaddr*)&ctx->buf->addr, 
        sizeof(sockaddr_in6),
        &ctx->ol,
        nullptr
    );

    if (res == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOGE(TAG, "Failed to send udp.[%s]", getErrMsg(err).c_str());
            releaseIoCtx(ctx);
            return -1;
        }
    }

    return 0;
}

int AioIntf::handleTapRead(IoCtx* ctx)
{
    if (ctx->dataLen <= 0) {
        LOGE(TAG, "Failed to read tap.[%s]", strerror(-ctx->dataLen));
    } else {
        tapLanPtr_->handleTapData(ctx);
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleTapWrite(IoCtx* ctx)
{
    if (ctx->dataLen <= 0) {
        LOGE(TAG, "Failed to write tap.[%s]", strerror(-ctx->dataLen));
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleUdpRecv(IoCtx* ctx)
{
    if (ctx->dataLen < 0) {
        LOGE(TAG, "Failed to recv udp.[%s]", strerror(-ctx->dataLen));
    } else {
        tapLanPtr_->handleUdpData(ctx);
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleUdpSend(IoCtx* ctx)
{
    if (ctx->dataLen < 0) {
        LOGE(TAG, "Failed to send udp.[%s]", strerror(-ctx->dataLen));
    }

    releaseIoCtx(ctx);
    return 0;
}

void AioIntf::aioWrk(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr)
{
    std::string errMsg;
    tapFd_ = tapFd;
    udpFd_ = udpFd;
    tapLanPtr_ = tapLanPtr;

    initAioIntf();

    if (!CreateIoCompletionPort(tapFd, hIOCP_, (ULONG_PTR)this, 0)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to bind tap to IOCP.[%s]", errMsg.c_str());
        g_cfgData.isRunning = false;
    }
    if (g_cfgData.swPortIntvl) {
        for (int i = 0; i < 4; ++i) {
            SockFd udpFd = tapLanPtr->udpSockPtrs_[i]->getFd();
            if (!CreateIoCompletionPort((HANDLE)udpFd, hIOCP_, (ULONG_PTR)this, 0)) {
                errMsg = getErrMsg(GetLastError());
                LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
                g_cfgData.isRunning = false;
            }
        }
    } else {
        if (!CreateIoCompletionPort((HANDLE)udpFd_, hIOCP_, (ULONG_PTR)this, 0)) {
            errMsg = getErrMsg(GetLastError());
            LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
            g_cfgData.isRunning = false;
        }
    }

    reqTapReadMultishot(tapFd_);
    reqUdpRecvMultishot(udpFd_);

    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED lpOverlapped;
    DWORD err;
    while (g_cfgData.isRunning) {
        BOOL ok = GetQueuedCompletionStatus(
            hIOCP_,
            &bytes,
            &key,
            &lpOverlapped,
            IO_WAIT_TIME * 1000
        );

        if (!lpOverlapped)
            continue;

        IoCtx* ctx = CONTAINING_RECORD(lpOverlapped, IoCtx, ol);
        if (ok) {
            ctx->dataLen = bytes;
        } else {
            err = GetLastError();
            if (err == WAIT_TIMEOUT)
                continue;
            else if (err == ERROR_OPERATION_ABORTED)
                break;

            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to GQCS.[%s]", errMsg.c_str());
            ctx->dataLen = -err;
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
