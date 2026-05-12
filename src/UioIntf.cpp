#include    "UioIntf.hpp"
#include    "TapLan.hpp"

UioIntf::UioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr)
: tapFd_(tapFd)
, udpFd_(udpFd)
, tapLanPtr_(tapLanPtr)
{
    ;
}

void SioIntf::udpWrk()
{
    UioCtx* ctx = &udpIoCtx_;
    int res;

    while (g_cfgData.running()) {
        res = udpRecv(udpFd_, ctx);
        if (res == -1) {
            continue;
        }

        tapLanPtr_->handleUdpData(ctx);
    }

    LOGI("udpWrk has exited.");
}

void SioIntf::tapWrk()
{
    UioCtx* ctx = &tapIoCtx_;
    int res;

    while (g_cfgData.running()) {
        res = tapRead(tapFd_, ctx);
        if (res == -1) {
            continue;
        }

        tapLanPtr_->handleTapData(ctx);
    }

    LOGI("tapWrk has exited.");
}

AioIntf::AioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr)
: UioIntf(tapFd, udpFd, tapLanPtr)
, isInitialized(false)
, dataBufs_(nullptr)
{
    ;
}

UioCtx* AioIntf::acquireIoCtx() {
    if (freeStack_.empty())
        return nullptr;

    UioCtx* ctx = freeStack_.top();
    freeStack_.pop();
    return ctx;
}

UioCtx* AioIntf::acquireIoCtx(size_t idx)
{
    if (idx >= DATA_BUF_NUM)
        return nullptr;

    return ioCtxs_[idx];
}

void AioIntf::releaseIoCtx(UioCtx* ctx)
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

int AioIntf::handleTapRead(UioCtx* ctx)
{
    if (ctx->dataLen <= 0) {
        if (ctx->dataLen != -EAGAIN && ctx->dataLen != -EWOULDBLOCK) {
            LOGE("Failed to read tap.[{}]", strerror(-ctx->dataLen));
        }
    } else {
        tapLanPtr_->handleTapData(ctx);
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleTapWrite(UioCtx* ctx)
{
    if (ctx->dataLen <= 0) {
        LOGE("Failed to write tap.[{}]", strerror(-ctx->dataLen));
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleUdpRecv(UioCtx* ctx)
{
    if (ctx->dataLen < 0) {
        LOGE("Failed to recv udp.[{}]", strerror(-ctx->dataLen));
    } else {
        tapLanPtr_->handleUdpData(ctx);
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleUdpSend(UioCtx* ctx)
{
    if (ctx->dataLen < 0) {
        LOGE("Failed to send udp.[{}]", strerror(-ctx->dataLen));
    }

    releaseIoCtx(ctx);
    return 0;
}