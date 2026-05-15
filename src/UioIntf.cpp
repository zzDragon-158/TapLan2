#include    "UioIntf.hpp"
#include    "TapLan.hpp"

UioIntf::UioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr)
: tapFd_(tapFd)
, udpFd_(udpFd)
, tapLanPtr_(tapLanPtr)
{
    ;
}

UioCtx* UioIntf::acquireIoCtx()
{
    LOGW("Not support to acquire ioctx.");

    return nullptr;
}

int UioIntf::univUdpSend(SockFd fd, UioCtx* ctx)
{
    LOGW("Not support to send to udp.");

    return -1;
}

int UioIntf::univTapWrite(TapFd fd, UioCtx* ctx)
{
    LOGW("Not support to write to tap.");

    return -1;
}

int SioIntf::univUdpSend(SockFd fd, UioCtx* ctx)
{
    int res = udpSend(fd, ctx);

    return res;
}

int SioIntf::univTapWrite(TapFd fd, UioCtx* ctx)
{
    int res = tapWrite(fd, ctx);

    return res;
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
    if (freeStack_.empty()) {
        LOGW("No available io ctx.");
        return nullptr;
    }

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
    Stats& stats = tapLanPtr_->tapRx_;

    if (ctx->dataLen <= 0) {
        if (ctx->dataLen != -EAGAIN && ctx->dataLen != -EWOULDBLOCK) {
            stats.errors++;
            LOGE("Failed to read tap.[{}]", strerror(-ctx->dataLen));
        }
    } else {
        stats.bytes += ctx->dataLen;
        stats.packets++;
        tapLanPtr_->handleTapData(ctx);
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleTapWrite(UioCtx* ctx)
{
    Stats& stats = tapLanPtr_->tapTx_;

    if (ctx->dataLen <= 0) {
        stats.errors++;
        LOGE("Failed to write tap.[{}]", strerror(-ctx->dataLen));
    } else {
        stats.bytes += ctx->dataLen;
        stats.packets++;
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleUdpRecv(UioCtx* ctx)
{
    Stats& stats = tapLanPtr_->udpRx_;

    if (ctx->dataLen < 0) {
        stats.errors++;
        LOGE("Failed to recv udp.[{}]", strerror(-ctx->dataLen));
    } else {
        stats.bytes += ctx->dataLen;
        stats.packets++;
        tapLanPtr_->handleUdpData(ctx);
    }

    releaseIoCtx(ctx);
    return 0;
}

int AioIntf::handleUdpSend(UioCtx* ctx)
{
    Stats& stats = tapLanPtr_->udpTx_;

    if (ctx->dataLen < 0) {
        stats.errors++;
        LOGE("Failed to send udp.[{}]", strerror(-ctx->dataLen));
    } else {
        stats.bytes += ctx->dataLen;
        stats.packets++;
    }

    releaseIoCtx(ctx);
    return 0;
}