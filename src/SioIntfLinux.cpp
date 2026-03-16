#include "SioIntf.hpp"

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

    return ctx;
}

int SioIntf::tapWrite(TapFd fd, Ctx* ctx)
{
    int res = ::write(fd, ctx->buf, ctx->dataLen);

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

    return res;
}
