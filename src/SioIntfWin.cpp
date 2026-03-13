#include    "SioIntf.hpp"

static const char* TAG = "[SioIntf]";

SioIntf::SioIntf()
{
    udpCurCtxIdx_ = tapCurCtxIdx_ = 0;
}

SioIntf::~SioIntf()
{

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
    if (ReadFile(fd, ctx->buf, 2048, &ctx->dataLen, &ctx->ol)) {
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
    Ctx* ctx = nullptr;

    if (GetOverlappedResult(fd, &tapCtxs_[tapCurCtxIdx_].ol, &tapCtxs_[tapCurCtxIdx_].dataLen, FALSE)) {
        ctx = &tapCtxs_[tapCurCtxIdx_];
    } else {
        LOGE(TAG, "Failed to get read result.");
    }
    tapReqRead(fd);

    return ctx;
}

int SioIntf::tapWrite(TapFd fd, Ctx* ctx)
{
    DWORD writeBytes;
    if (WriteFile(tapFd, ctx->buf, ctx->dataLen, &writeBytes, &ctx->ol)) {
        return writeBytes;
    }

    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ctx->ol.hEvent, IO_WAIT_TIME * 1000) == WAIT_OBJECT_0) {
            if (!GetOverlappedResult(tapFd, &ctx->ol, &writeBytes, TRUE)) {
                LOGE(TAG, "Failed to write tap.");
            }
        } else {
            LOGE(TAG, "Failed to wait for writing tap.");
        }
    } else {
        LOGE(TAG, "Failed to write tap.[%s]", getErrMsg(err).c_str());
        return -1;
    }

    return writeBytes;
}

int SioIntf::udpReqRecv(SocketFd fd)
{
    int ret;
    HANDLE& udpRecvEv = events_[EVENT_UDP_RECV];
    Ctx* ctx = &udpCtxs_[(udpCurCtxIdx_ = ++udpCurCtxIdx_ % 2)];

    udpRecvEv = ctx->ol.hEvent;
    ctx->wsaBuf.len = 2048;
    ctx->addrLen = sizeof(sockaddr_in6);
    DWORD flags = 0;
    ret = WSARecvFrom(fd,
                      &ctx->wsaBuf,
                      1,
                      &ctx->dataLen,
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
    WSAGetOverlappedResult(
        fd, 
        &ctx->ol, 
        &ctx->dataLen, 
        FALSE,
        &flags
    );
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
