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

const size_t DATA_BUF_NUM = 1024;
const int MAX_RECV_REQ = 256;
const int MAX_READ_REQ = 256;

// for Linux
const size_t IOURING_SIZE = 1024;

class TapLan;

struct IoCtx {
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

    IoCtx() {
        memset(this, 0, sizeof(IoCtx));
    }
};

class IoIntf {
public:
    IoIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);

    virtual IoCtx* acquireIoCtx() { return nullptr; };

    virtual int reqUdpSend(SockFd fd, IoCtx* ctx) { return -1; };
    virtual int reqTapWrite(TapFd fd, IoCtx* ctx) { return -1; };

protected:
    TapFd tapFd_;
    SockFd udpFd_;
    TapLan* tapLanPtr_;
};

class SioIntf: public IoIntf {
public:
    SioIntf() = delete;
    SioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);
    ~SioIntf();

    void udpWrk();
    void tapWrk();

private:
    IoCtx udpIoCtx_;
    IoCtx tapIoCtx_;

    int tapRead(TapFd fd, IoCtx* ctx = nullptr);
    int tapWrite(TapFd fd, IoCtx* ctx);
    int udpRecv(SockFd fd, IoCtx* ctx = nullptr);
    int udpSend(SockFd fd, IoCtx* ctx);

    int reqUdpSend(SockFd fd, IoCtx* ctx) { return udpSend(fd, ctx); };
    int reqTapWrite(TapFd fd, IoCtx* ctx) { return tapWrite(fd, ctx); };
};

class AioIntf: public IoIntf {
public:
    AioIntf() = delete;
    AioIntf(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);
    ~AioIntf();

    void aioWrk();

private:
    uint8_t* dataBufs_;
    std::vector<IoCtx*> ioCtxs_;
    std::stack<IoCtx*> freeStack_;
#ifdef      _WIN32
    HANDLE hIOCP_;
#elif       __linux__
    io_uring* ring_;
    io_uring_buf_ring* bufRing_;
    int bufRingMask_;
    uint16_t advanceCnt_;
#endif
    int initAioIntf();

    IoCtx* acquireIoCtx();
    IoCtx* acquireIoCtx(size_t idx);
    void releaseIoCtx(IoCtx* ctx);

    int reqTapRead(TapFd fd, IoCtx* ctx = nullptr);
    int reqTapReadMultishot(TapFd fd);
    int reqTapWrite(TapFd fd, IoCtx* ctx);
    int reqUdpRecv(SockFd fd, IoCtx* ctx = nullptr);
    int reqUdpRecvMultishot(SockFd fd);
    int reqUdpSend(SockFd fd, IoCtx* ctx);

    int handleTapRead(IoCtx* ctx);
    int handleTapWrite(IoCtx* ctx);
    int handleUdpRecv(IoCtx* ctx);
    int handleUdpSend(IoCtx* ctx);
};
