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

#define     AioIntfPtr      AioIntf::ptr()

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
    struct Ctx {
        char token;
        AioIntf* owner;
        unsigned short bufId;
        Buf* buf;
        INT bufLen;
        OVERLAPPED ol;
        WSABUF wsaBuf;

        Ctx(){
            memset(this, 0, sizeof(Ctx));
        }
    };

    HANDLE hIOCP_;

    static AioIntf* ptr();
    AioIntf();
    ~AioIntf();
    Ctx* acquireAioCtx();
    Ctx* acquireAioCtx(size_t idx);
    void releaseAioCtx(Ctx* ctx);
    int reqTapRead(TapFd fd, Ctx* ctx = nullptr);
    int reqTapReadMultishot(TapFd fd);
    int reqTapWrite(TapFd fd, Ctx* ctx);
    int reqUdpRecv(SocketFd fd, Ctx* ctx = nullptr);
    int reqUdpRecvMultishot(SocketFd fd);
    int reqUdpSend(SocketFd fd, Ctx* ctx);

private:
    uint8_t* dataBufs_;
    TapFd tapFd_;
    SocketFd udpFd_;
    std::vector<Ctx*> ioCtxs_;
    std::stack<Ctx*> freeStack_;
};

#elif       __linux__
class AioIntf {
public:
    struct Buf {
        io_uring_recvmsg_out ro;
        sockaddr_storage addr;
        char payload[];
    };
    struct Ctx {
        char token;
        AioIntf* owner;
        io_uring* ring;
        unsigned short bufId;
        Buf *buf;
        int bufLen;
        msghdr msgHdr;
        iovec iov;

        Ctx() {
            memset(this, 0, sizeof(Ctx));
            // bufId = -1;
        };
    };

    io_uring ring_;
    io_uring_buf_ring* bufRing_;
    int bufRingMask_;

    static AioIntf* ptr();
    AioIntf();
    ~AioIntf();
    Ctx* acquireAioCtx();
    Ctx* acquireAioCtx(size_t idx);
    void releaseAioCtx(Ctx* ctx);
    int reqTapRead(TapFd fd, Ctx* ctx = nullptr);
    int reqTapReadMultishot(TapFd fd);
    int reqTapWrite(TapFd fd, Ctx* ctx);
    int reqUdpRecv(SocketFd fd, Ctx* ctx = nullptr);
    int reqUdpRecvMultishot(SocketFd fd);
    int reqUdpSend(SocketFd fd, Ctx* ctx);
    void submitAioReq() { io_uring_submit(&ring_); };

private:
    uint8_t* dataBufs_;
    TapFd tapFd_;
    SocketFd udpFd_;
    std::vector<Ctx*> ioCtxs_;
    std::stack<Ctx*> freeStack_;
};

#else

#endif

inline AioIntf* AioIntf::ptr()
{
    static AioIntf ins;
    return &ins;
}
