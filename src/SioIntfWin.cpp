#include    "SioIntf.hpp"

static const char* TAG = "[SioIntf]";

SioIntf::SioIntf(): udpCurCtxIdx_(0), tapCurCtxIdx_(0)
{
    ;
}

SioIntf::~SioIntf()
{
    ;
}

int SioIntf::init(SocketFd ufd, TapFd tfd)
{
    udpReqRecv(ufd);
    tapReqRead(tfd);

    return 0;
}

int SioIntf::tapReqRead(TapFd fd)
{
    HANDLE& tapReadEv = events_[EVENT_TAP_READ];
    Ctx* ctx = &tapCtxs_[(tapCurCtxIdx_ = ++tapCurCtxIdx_ % 2)];
    tapReadEv = ctx->ol.hEvent;
    events_[EVENT_TAP_READ + NUMS_OF_EVENT] = ctx->ol.hEvent;

    if (ReadFile(fd, ctx->buf, DATA_BUF_SIZE, nullptr, &ctx->ol)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            LOGE(TAG, "Failed to read tap.[%s]", getErrMsg(err).c_str());
            return -1;
        }
    }

    return 0;
}

SioIntf::Ctx* SioIntf::tapRead(TapFd fd)
{
    Ctx* ctx = &tapCtxs_[tapCurCtxIdx_];

    if (!GetOverlappedResult(fd, &ctx->ol, &ctx->dataLen, FALSE)) {
        ctx->dataLen = -1;
        LOGE(TAG, "Failed to get read result.");
    }
    tapReqRead(fd);

    return ctx;
}

int SioIntf::tapWrite(TapFd fd, Ctx* ctx)
{
    DWORD err;

    if (WriteFile(fd, ctx->buf, ctx->dataLen, nullptr, &ctx->ol)) {
        return ctx->dataLen;
    }

    err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    DWORD res = WaitForSingleObject(ctx->ol.hEvent, IO_WAIT_TIME * 1000);
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

    return ctx->dataLen;
}

int SioIntf::udpReqRecv(SocketFd fd)
{
    int ret;
    HANDLE& udpRecvEv = events_[EVENT_UDP_RECV];
    Ctx* ctx = &udpCtxs_[(udpCurCtxIdx_ = ++udpCurCtxIdx_ % 2)];
    udpRecvEv = ctx->ol.hEvent;
    events_[EVENT_UDP_RECV + NUMS_OF_EVENT] = ctx->ol.hEvent;

    ctx->addrLen = sizeof(sockaddr_in6);
    ctx->wsaBuf.len = DATA_BUF_SIZE;
    DWORD flags = 0;
    ret = WSARecvFrom(fd,
                      &ctx->wsaBuf,
                      1,
                      nullptr,
                      &flags,
                      reinterpret_cast<sockaddr*>(&ctx->addr),
                      &ctx->addrLen,
                      &ctx->ol,
                      nullptr);
    if (ret == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOGE(TAG, "Failed to recv udp.[%s]", getErrMsg(err).c_str());
            return -1;
        }
    }

    return 0;
}

SioIntf::Ctx* SioIntf::udpRecv(SocketFd fd)
{
    Ctx* ctx = &udpCtxs_[udpCurCtxIdx_];
    DWORD flags = 0;
    if (!WSAGetOverlappedResult(fd, 
                                &ctx->ol, 
                                &ctx->dataLen, 
                                FALSE,
                                &flags)) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            ctx->dataLen = -1;
            LOGE(TAG, "Failed to get recv result.[%s]", getErrMsg(err));
        }
    }
    udpReqRecv(fd);

    return ctx;
}

int SioIntf::udpSend(SocketFd fd, Ctx* ctx)
{
    int ret;

    ctx->addrLen = sizeof(sockaddr_in6);
    ret = sendto(fd,
                 ctx->buf,
                 ctx->dataLen,
                 0,
                 reinterpret_cast<sockaddr*>(&ctx->addr),
                 ctx->addrLen);

    return ret;
}
