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

#ifdef      _WIN32
class AioIntf {
public:
    struct Buf {
        sockaddr_in6 addr;
        INT addrLen;
        char payload[];
    };
    struct IoCtx {
        char token;
        AioIntf* owner;
        unsigned short bufId;
        Buf* buf;
        INT bufLen;
        OVERLAPPED ol;
        WSABUF wsaBuf;

        IoCtx(){
            memset(this, 0, sizeof(IoCtx));
        }
    };

    HANDLE hIOCP_;

    static AioIntf* ptr();
    AioIntf();
    ~AioIntf();
    IoCtx* acquireIoCtx();
    IoCtx* acquireIoCtx(size_t idx);
    void releaseIoCtx(IoCtx* ctx);
    int reqTapRead(TapFd fd, IoCtx* ctx = nullptr);
    int reqTapReadMultishot(TapFd fd);
    int reqTapWrite(TapFd fd, IoCtx* ctx);
    int reqUdpRecv(SockFd fd, IoCtx* ctx = nullptr);
    int reqUdpRecvMultishot(SockFd fd);
    int reqUdpSend(SockFd fd, IoCtx* ctx);

private:
    uint8_t* dataBufs_;
    TapFd tapFd_;
    SockFd udpFd_;
    std::vector<IoCtx*> ioCtxs_;
    std::stack<IoCtx*> freeStack_;
};

#elif       __linux__
class TapLan;

struct IoCtx {
    char ref;
    char token;
    unsigned short bufId;
    struct Buf {
        io_uring_recvmsg_out ro;
        sockaddr_in6 addr;
        char payload[];
    } *buf;
    int bufLen;
    msghdr msgHdr;
    iovec iov;

    IoCtx() {
        memset(this, 0, sizeof(*this));
    };
};

class IoIntf {
public:
    virtual IoCtx* acquireIoCtx() { return nullptr; };

    virtual int reqUdpSend(SockFd fd, IoCtx* ctx) { return -1; };
    virtual int reqTapWrite(TapFd fd, IoCtx* ctx) { return -1; };
};

class AioIntf: public IoIntf {
public:

    AioIntf();
    ~AioIntf();
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

    void aioWrk(TapFd tapFd, SockFd udpFd, TapLan* tapLanPtr);

private:
    uint8_t* dataBufs_ = nullptr;
    TapFd tapFd_ = -1;
    SockFd udpFd_ = -1;
    TapLan* tapLanPtr_ = nullptr;
    uint16_t advanceCnt_ = 0;
    std::vector<IoCtx*> ioCtxs_;
    std::stack<IoCtx*> freeStack_;

    io_uring* ring_;
    io_uring_buf_ring* bufRing_;
    int bufRingMask_;
};

#else

#endif
