#include    "SioIntf.hpp"

static const char* TAG = "[SioIntf]";

SioIntf::SioIntf()
{
    ;
}

SioIntf::~SioIntf()
{
    ;
}

SioIntf::Ctx* SioIntf::tapRead(TapFd fd)
{
    Ctx* ctx = &tapSioCtx_;
    DWORD err, res;

    if (ReadFile(fd, ctx->buf, DATA_BUF_SIZE, &ctx->dataLen, &ctx->ol)) {
        goto r_success;
    }

    err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
        goto r_fail;
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
        goto r_fail;
    }

    if (!GetOverlappedResult(fd, &ctx->ol, &ctx->dataLen, FALSE)) {
        err = GetLastError();
        if (err != ERROR_OPERATION_ABORTED) {
            LOGE(TAG, "Failed to get read result.[%s]", getErrMsg(err).c_str());
        }
        goto r_fail;
    }

    goto r_success;

r_fail:
    ctx->dataLen = -1;
r_success:
    return ctx;
}

int SioIntf::tapWrite(TapFd fd, Ctx* ctx)
{
    DWORD err;
    DWORD res;
    DWORD writeBytes;

    if (WriteFile(fd, ctx->buf, ctx->dataLen, &writeBytes, &ctx->ol)) {
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

SioIntf::Ctx* SioIntf::udpRecv(SocketFd fd)
{
    DWORD err;
    DWORD res;
    DWORD flags = 0;
    Ctx* ctx = &udpSioCtx_;

    ctx->addrLen = sizeof(ctx->addr);
    ctx->wsaBuf.len = DATA_BUF_SIZE;
    res = WSARecvFrom(fd,
                      &ctx->wsaBuf,
                      1,
                      &ctx->dataLen,
                      &flags,
                      reinterpret_cast<sockaddr*>(&ctx->addr),
                      &ctx->addrLen,
                      nullptr,
                      nullptr);

    if (res == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEINTR) {
            LOGE(TAG, "Failed to recvfrom udp.[%s]", getErrMsg(err).c_str());
        }
        goto r_fail;
    }

    goto r_success;

r_fail:
    ctx->dataLen = -1;
r_success:
    return ctx;
}

int SioIntf::udpSend(SocketFd fd, Ctx* ctx)
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
                    reinterpret_cast<sockaddr*>(&ctx->addr),
                    sizeof(ctx->addr),
                    nullptr,
                    nullptr);

    if (res == SOCKET_ERROR) {
        err = WSAGetLastError();
        LOGE(TAG, "Failed to sendto udp.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    return sendBytes;
}

int SioIntf::tcpRecv(Ctx* ctx)
{
    DWORD recvBytes;

    ctx->wsaBuf.len = DATA_BUF_SIZE;
    DWORD flags = 0;
    int res = WSARecv(
        static_cast<SocketFd>(*ctx->sockPtr),
        &ctx->wsaBuf,
        1,
        &recvBytes,
        &flags,
        nullptr,
        nullptr
    );
    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEINTR) {
            LOGE(TAG, "Failed to recv tcp.[%s]", getErrMsg(err).c_str());
        }
        return -1;
    }

    return recvBytes;
}

int SioIntf::tcpSend(Ctx* ctx)
{
    DWORD sendBytes;

    ctx->wsaBuf.len = ctx->dataLen;
    int res = WSASend(
        static_cast<SocketFd>(*ctx->sockPtr),
        &ctx->wsaBuf,
        1,
        &sendBytes,
        0,
        nullptr,
        nullptr
    );

    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEINTR) {
            LOGE(TAG, "Failed to send tcp.[%s]", getErrMsg(err).c_str());
        }
        return -1;
    }

    return sendBytes;
}
