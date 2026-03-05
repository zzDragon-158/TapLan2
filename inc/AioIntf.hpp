#pragma     once
#include    <cstdint>
#include    <cstring>
#include    <cstdlib>
#include    <vector>
#include    <stack>
#include    "Common.hpp"
#include    "LogMgr.hpp"
#include    "Socket.hpp"
#include    "TapDev.hpp"

#define     AioIntfPtr      AioIntf::ptr()

const size_t DATA_BUF_SIZE = 4096;
const size_t DATA_BUF_NUM = 256;
const int MAX_RECV_REQ = 16;
const int MAX_READ_REQ = 16;

#ifdef      _WIN32
class AioIntf {
public:
    struct Ctx {
        AioIntf* owner;
        OVERLAPPED overlapped;
        sockaddr_in6 addr;
        INT addrLen;
        uint8_t* buf;
        DWORD bufLen;
        uint8_t token;

        Ctx(){
            memset(this, 0, sizeof(Ctx));
        }
    };

    HANDLE hIOCP_;

    static AioIntf* ptr();
    AioIntf();
    ~AioIntf();
    Ctx* acquireAioCtx();
    void releaseAioCtx(Ctx* ctx);
    int reqTapRead(TapFd fd);
    int reqTapWrite(TapFd fd, Ctx* ctx);
    int reqUdpRecv(SocketFd fd);
    int reqUdpSend(SocketFd fd, Ctx* ctx);

private:
    uint8_t* dataBufs_;
    std::vector<Ctx*> ioCtxs_;
    std::stack<Ctx*> freeStack_;
};

#elif       __linux__
class AioIntf {
public:
    struct Ctx {
        uint8_t token;
        AioIntf* owner;
        io_uring* ring;
        // int bufId;
        uint8_t* buf;
        int bufLen;    // for store cqe->res
        msghdr msgHdr;
        sockaddr_in6 addr;
        iovec iov;

        Ctx() {
            memset(this, 0, sizeof(Ctx));
            // bufId = -1;
        };
    };

    io_uring ring_;

    static AioIntf* ptr();
    AioIntf();
    ~AioIntf();
    Ctx* acquireAioCtx();
    void releaseAioCtx(Ctx* ctx);
    int reqTapRead(TapFd fd);
    int reqTapWrite(TapFd fd, Ctx* ctx);
    int reqUdpRecv(SocketFd fd);
    int reqUdpSend(SocketFd fd, Ctx* ctx);
    void submitAioReq() { io_uring_submit(&ring_); };

private:
    uint8_t* dataBufs_;
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
