#pragma     once
#include    <cstdint>
#include    <vector>
#include    <stack>
#if defined(__linux__)
#include    "liburing.h"
#endif
#include    "Common.hpp"
#include    "DataSec.hpp"

class TapLan;

enum class UioCtxToken: char {
    Unused = 0,
    UdpRecvMultishot,
    UdpRecv,
    UdpSend,
    TapRead,
    TapWrite,
};

struct UioCtx {
    char ref;
    UioCtxToken token;
    unsigned short bufId;
#ifdef      _WIN32
    struct Buf {
        sockaddr_in6 addr;
        Nonce nonce;
        char payload[0];
    } *buf;
    DWORD dataLen;
    socklen_t addrLen;
    OVERLAPPED ol;
    WSABUF wsaBuf;
#elif       __linux__
    struct Buf {
        io_uring_recvmsg_out ro;
        sockaddr_in6 addr;
        Nonce nonce;
        char payload[0];
    } *buf;
    socklen_t addrLen;
    int dataLen;
    msghdr msgHdr;
    iovec iov;
#endif
};

class UioIntf {
public:
    UioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);

    virtual UioCtx* acquireIoCtx();

    virtual int univUdpSend(SockFd fd, UioCtx* ctx);
    virtual int univTapWrite(TapFd fd, UioCtx* ctx);

protected:
    static constexpr size_t DATA_BUF_SIZE = 2048;
    static constexpr size_t DATA_BUF_NUM = 1024;
    static constexpr size_t MAX_RECV_REQ = 256;
    static constexpr size_t MAX_READ_REQ = 8;
    static constexpr size_t PAYLOAD_SIZE = DATA_BUF_SIZE - sizeof(UioCtx::Buf);
    /* START_UDP_BUF_IDX        START_TAP_BUF_IDX        START_FREE_BUF_IDX         */
    /* |------------------------|------------------------|------------------------| */
    /* |          udp           |          tap           |        freestack       | */
    static constexpr size_t START_UDP_BUF_IDX = 0;
    static constexpr size_t START_TAP_BUF_IDX = MAX_RECV_REQ;
    static constexpr size_t START_FREE_BUF_IDX = MAX_READ_REQ + MAX_RECV_REQ;

    TapFd tapFd_ = INVALID_TAPFD;
    SockFd udpFd_ = INVALID_SOCKFD;
    TapLan* tapLanPtr_ = nullptr;

private:
    static constexpr char TAG[] = "[UioIntf]";
};

class SioIntf: public UioIntf {
public:
    SioIntf() = delete;
    SioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);
    ~SioIntf();

    void udpWrk();
    void tapWrk();

private:
    static constexpr char TAG[] = "[SioIntf]";
    UioCtx udpIoCtx_{};
    UioCtx tapIoCtx_{};

    int tapRead(TapFd fd, UioCtx* ctx = nullptr);
    int tapPollRead(TapFd fd, UioCtx* ctx);
    int tapWrite(TapFd fd, UioCtx* ctx);
    int udpRecv(SockFd fd, UioCtx* ctx = nullptr);
    int udpPollRecv(SockFd fd, UioCtx* ctx);
    int udpSend(SockFd fd, UioCtx* ctx);

    int univUdpSend(SockFd fd, UioCtx* ctx) { return udpSend(fd, ctx); };
    int univTapWrite(TapFd fd, UioCtx* ctx) { return tapWrite(fd, ctx); };
};

class AioIntf: public UioIntf {
public:
    AioIntf() = delete;
    AioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);
    ~AioIntf();

    int initAioIntf();
    void aioWrk();

private:
    static constexpr char TAG[] = "[AioIntf]";
    bool isInitialized = false;
    uint8_t* dataBufs_ = nullptr;
    std::vector<UioCtx*> ioCtxs_;
    std::stack<UioCtx*> freeStack_;
#ifdef      _WIN32
    HANDLE hIOCP_ = INVALID_HANDLE_VALUE;
#elif       __linux__
    static constexpr size_t IOURING_SIZE = (MAX_READ_REQ + MAX_RECV_REQ);
    io_uring* ring_ = nullptr;
    io_uring_buf_ring* bufRing_ = nullptr;
    static constexpr int bufRingMask_ = MAX_RECV_REQ - 1;
    uint16_t advanceCnt_ = 0;
#endif
    UioCtx* acquireIoCtx();
    UioCtx* acquireIoCtx(size_t idx);
    void releaseIoCtx(UioCtx* ctx);

    int reqTapRead(TapFd fd, UioCtx* ctx = nullptr);
    int reqTapReadMultishot(TapFd fd);
    int reqTapWrite(TapFd fd, UioCtx* ctx);
    int reqUdpRecv(SockFd fd, UioCtx* ctx = nullptr);
    int reqUdpRecvMultishot(SockFd fd);
    int reqUdpSend(SockFd fd, UioCtx* ctx);

    int handleTapRead(UioCtx* ctx);
    int handleTapWrite(UioCtx* ctx);
    int handleUdpRecv(UioCtx* ctx);
    int handleUdpSend(UioCtx* ctx);

    int univUdpSend(SockFd fd, UioCtx* ctx) { return reqUdpSend(fd, ctx); };
    int univTapWrite(TapFd fd, UioCtx* ctx) { return reqTapWrite(fd, ctx); };
};
