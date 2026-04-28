#include    "UioIntf.hpp"
#include    "TapLan.hpp"

SioIntf::SioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr)
: UioIntf(tapFd, udpFd, tapLanPtr)
{
    tapIoCtx_.buf = reinterpret_cast<UioCtx::Buf*>(new char [DATA_BUF_SIZE]);
    tapIoCtx_.ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    tapIoCtx_.wsaBuf = { PAYLOAD_SIZE, tapIoCtx_.buf->payload };

    udpIoCtx_.buf = reinterpret_cast<UioCtx::Buf*>(new char [DATA_BUF_SIZE]);
    udpIoCtx_.ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    udpIoCtx_.wsaBuf = { PAYLOAD_SIZE, udpIoCtx_.buf->payload };
}

SioIntf::~SioIntf()
{
    delete[] tapIoCtx_.buf;
    CloseHandle(tapIoCtx_.ol.hEvent);

    delete[] udpIoCtx_.buf;
    CloseHandle(udpIoCtx_.ol.hEvent);
}

int SioIntf::tapRead(TapFd fd, UioCtx* ctx)
{
    DWORD err, res;

    if (ReadFile(fd, ctx->buf->payload, PAYLOAD_SIZE, &ctx->dataLen, &ctx->ol)) {
        return ctx->dataLen;
    }

    err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    res = WaitForSingleObject(ctx->ol.hEvent, INFINITE);
    if (res != WAIT_OBJECT_0) {
        switch (res) {
        case WAIT_TIMEOUT:
            LOGE(TAG, "read tap timeout.");
            break;

        case WAIT_FAILED:
            err = GetLastError();
            LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err));
            break;

        default:
            LOGE(TAG, "Unknown error[%u].", res);
            break;
        }
        return -1;
    }

    if (!GetOverlappedResult(fd, &ctx->ol, &ctx->dataLen, FALSE)) {
        err = GetLastError();
        if (err != ERROR_OPERATION_ABORTED) {
            LOGE(TAG, "Failed to get read result.[%s]", getErrMsg(err).c_str());
        }
        return -1;
    }

    return ctx->dataLen;
}

int SioIntf::tapWrite(TapFd fd, UioCtx* ctx)
{
    DWORD err;
    DWORD writeBytes;
    DWORD res;

    if (WriteFile(fd, ctx->buf->payload, ctx->dataLen, &writeBytes, &ctx->ol)) {
        return writeBytes;
    }

    err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    res = WaitForSingleObject(ctx->ol.hEvent, INFINITE);
    if (res != WAIT_OBJECT_0) {
        switch (res) {
        case WAIT_TIMEOUT:
            LOGE(TAG, "Write tap timeout.");
            break;

        case WAIT_FAILED:
            err = GetLastError();
            LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err));
            break;

        default:
            LOGE(TAG, "Unknown error[%u].", res);
            break;
        }
        return -1;
    }

    if (!GetOverlappedResult(fd, &ctx->ol, &writeBytes, FALSE)) {
        err = GetLastError();
        LOGE(TAG, "Failed to get write result.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    return writeBytes;
}

int SioIntf::udpRecv(SockFd fd, UioCtx* ctx)
{
    ctx->addrLen = sizeof(ctx->buf->addr);
    ctx->wsaBuf.len = PAYLOAD_SIZE;

    DWORD flags = 0;
    DWORD res = WSARecvFrom(fd,
                      &ctx->wsaBuf,
                      1,
                      &ctx->dataLen,
                      &flags,
                      reinterpret_cast<sockaddr*>(&ctx->buf->addr),
                      &ctx->addrLen,
                      nullptr,
                      nullptr);

    if (res == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSAEINTR) {
            LOGE(TAG, "Failed to recvfrom udp.[%s]", getErrMsg(err).c_str());
        }
        return -1;
    }

    return ctx->dataLen;
}

int SioIntf::udpSend(SockFd fd, UioCtx* ctx)
{
    int res;
    DWORD err;
    DWORD sendBytes;

    ctx->wsaBuf.len = ctx->dataLen;
    res = WSASendTo(fd,
                    &ctx->wsaBuf,
                    1,
                    &sendBytes,
                    0,
                    reinterpret_cast<sockaddr*>(&ctx->buf->addr),
                    sizeof(ctx->buf->addr),
                    nullptr,
                    nullptr);

    if (res == SOCKET_ERROR) {
        err = WSAGetLastError();
        LOGE(TAG, "Failed to sendto udp.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    return sendBytes;
}

AioIntf::~AioIntf()
{
    for (auto ctx: ioCtxs_) delete ctx;
    _aligned_free(dataBufs_);
}

int AioIntf::initAioIntf()
{
    if (isInitialized) {
        return 0;
    }

    std::string errMsg;

    size_t totalDatBufSize = DATA_BUF_NUM * DATA_BUF_SIZE;
    dataBufs_ = (uint8_t*)_aligned_malloc(totalDatBufSize, 64);
    if (dataBufs_ == nullptr) {
        g_cfgData.running() = false;
        LOGF(TAG, "Cant allocate [%u] memory.", totalDatBufSize);
        return -1;
    }

    for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
        UioCtx* ctx = new UioCtx();

        ctx->bufId = i;
        ctx->buf = reinterpret_cast<UioCtx::Buf*>(dataBufs_ + i * DATA_BUF_SIZE);
        ZeroMemory(ctx->buf, sizeof(UioCtx::Buf));
        ctx->wsaBuf.buf = ctx->buf->payload;

        ioCtxs_.push_back(ctx);
        if (i > START_FREE_BUF_IDX)
            freeStack_.push(ctx);
    }

    hIOCP_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (hIOCP_ == nullptr) {
        g_cfgData.running() = false;
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to create IOCP.[%s]", errMsg.c_str());
        return -1;
    }

    if (!CreateIoCompletionPort(tapFd_, hIOCP_, (ULONG_PTR)this, 0)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to bind tap to IOCP.[%s]", errMsg.c_str());
        g_cfgData.running() = false;
        return -1;
    }

    if (g_cfgData.swPortIntvl()) {
        for (int i = 0; i < 4; ++i) {
            UdpSock* udpSockPtr = tapLanPtr_->udpSockPtrs_[i];
            if (udpSockPtr == nullptr) {
                continue;
            }

            SockFd udpFd = udpSockPtr->getFd();
            if (!CreateIoCompletionPort((HANDLE)udpFd, hIOCP_, (ULONG_PTR)this, 0)) {
                errMsg = getErrMsg(GetLastError());
                LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
                g_cfgData.running() = false;
                return -1;
            }
        }
    } else {
        if (!CreateIoCompletionPort((HANDLE)udpFd_, hIOCP_, (ULONG_PTR)this, 0)) {
            errMsg = getErrMsg(GetLastError());
            LOGF(TAG, "Failed to bind udp to IOCP.[%s]", errMsg.c_str());
            g_cfgData.running() = false;
            return -1;
        }
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
    ctx->token = TOKEN_UDP_RECV;
    ctx->addrLen = sizeof(ctx->buf->addr);
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
        &ctx->addrLen,
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
    for (int idx = START_UDP_BUF_IDX; idx < START_UDP_BUF_IDX + MAX_RECV_REQ; ++idx) {
        UioCtx* ctx = acquireIoCtx(idx);
        reqUdpRecv(fd, ctx);
    }

    return 0;
}

int AioIntf::reqUdpSend(SockFd fd, UioCtx* ctx)
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

void AioIntf::aioWrk()
{
    std::string errMsg;

    if (initAioIntf() != 0) {
        return ;
    }

    reqTapReadMultishot(tapFd_);
    reqUdpRecvMultishot(udpFd_);

    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED lpOverlapped;
    DWORD err;
    while (g_cfgData.running()) {
        BOOL ok = GetQueuedCompletionStatus(
            hIOCP_,
            &bytes,
            &key,
            &lpOverlapped,
            IO_WAIT_TIME * 1000
        );

        if (!lpOverlapped)
            continue;

        UioCtx* ctx = CONTAINING_RECORD(lpOverlapped, UioCtx, ol);
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
