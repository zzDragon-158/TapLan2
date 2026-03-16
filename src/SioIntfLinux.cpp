#include "SioIntf.hpp"

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

    ctx->dataLen = ::read(fd, ctx->buf, DATA_BUF_SIZE);
    if (ctx->dataLen == -1) {
        int err = errno;
        LOGE(TAG, "Failed to read tap.[%s]", strerror(err));
    }

    return ctx;
}

int SioIntf::tapWrite(TapFd fd, Ctx* ctx)
{
    int res = ::write(fd, ctx->buf, ctx->dataLen);
    if (res == -1) {
        int err = errno;
        LOGE(TAG, "Failed to write tap.[%s]", strerror(err));
    }

    return res;
}

SioIntf::Ctx* SioIntf::udpRecv(SocketFd fd)
{
    Ctx* ctx = &udpSioCtx_;

    ctx->addrLen = sizeof(ctx->addr);
    ctx->dataLen = ::recvfrom(fd,
                              ctx->buf,
                              DATA_BUF_SIZE,
                              0,
                              reinterpret_cast<sockaddr*>(&ctx->addr),
                              &ctx->addrLen);
    if (ctx->dataLen == -1) {
        int err = errno;
        LOGE(TAG, "Failed to recvfrom udp.[%s]", strerror(err));
    }

    return ctx;
}

int SioIntf::udpSend(SocketFd fd, Ctx* ctx)
{
    int res = ::sendto(fd,
                       ctx->buf,
                       ctx->dataLen,
                       0,
                       reinterpret_cast<sockaddr*>(&ctx->addr),
                       sizeof(ctx->addr));
    if (res == -1) {
        int err = errno;
        LOGE(TAG, "Failed to sendto udp.[%s]", strerror(err));
    }

    return res;
}
