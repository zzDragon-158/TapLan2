#pragma     once
#include    <cstdint>
#include    <cstring>
#include    <cstdlib>
#include    <vector>
#include    <stack>
#include    "Common.hpp"
#include    "LogMgr.hpp"
#include    "BsdSock.hpp"
#include    "TapDev.hpp"

class TapLan;

struct UioCtx {
    char ref;
    char token;
    unsigned short bufId;
#ifdef      _WIN32
    struct Buf {
        sockaddr_in6 addr;
        uint8_t resv[2];
        char payload[];
    } *buf;
    DWORD dataLen;
    socklen_t addrLen;
    OVERLAPPED ol;
    WSABUF wsaBuf;
#elif       __linux__
    struct Buf {
        io_uring_recvmsg_out ro;
        sockaddr_in6 addr;
        char payload[];
    } *buf;
    socklen_t addrLen;
    int dataLen;
    msghdr msgHdr;
    iovec iov;
#endif

    UioCtx() {
        memset(this, 0, sizeof(UioCtx));
    }
};

class UioIntf {
public:
    UioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);

    virtual UioCtx* acquireIoCtx() { return nullptr; };

    virtual int reqUdpSend(SockFd fd, UioCtx* ctx) { return -1; };
    virtual int reqTapWrite(TapFd fd, UioCtx* ctx) { return -1; };

protected:
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

    TapFd tapFd_;
    SockFd udpFd_;
    TapLan* tapLanPtr_;
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
    UioCtx udpIoCtx_;
    UioCtx tapIoCtx_;

    int tapRead(TapFd fd, UioCtx* ctx = nullptr);
    int tapPollRead(TapFd fd, UioCtx* ctx);
    int tapWrite(TapFd fd, UioCtx* ctx);
    int udpRecv(SockFd fd, UioCtx* ctx = nullptr);
    int udpPollRecv(SockFd fd, UioCtx* ctx);
    int udpSend(SockFd fd, UioCtx* ctx);

    int reqUdpSend(SockFd fd, UioCtx* ctx) { return udpSend(fd, ctx); };
    int reqTapWrite(TapFd fd, UioCtx* ctx) { return tapWrite(fd, ctx); };
};

class AioIntf: public UioIntf {
public:
    AioIntf() = delete;
    AioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);
    ~AioIntf();

    void aioWrk();

private:
    static constexpr char TAG[] = "[AioIntf]";
    uint8_t* dataBufs_;
    std::vector<UioCtx*> ioCtxs_;
    std::stack<UioCtx*> freeStack_;
#ifdef      _WIN32
    HANDLE hIOCP_;
#elif       __linux__
    static constexpr size_t IOURING_SIZE = (MAX_READ_REQ + MAX_RECV_REQ);
    io_uring* ring_;
    io_uring_buf_ring* bufRing_;
    int bufRingMask_;
    uint16_t advanceCnt_;
#endif
    int initAioIntf();

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
};
