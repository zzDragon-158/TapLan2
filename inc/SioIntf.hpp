#pragma     once
#include    "Common.hpp"
#include    "TapDev.hpp"
#include    "Socket.hpp"

#define     SioIntfPtr      SioIntf::ptr()

enum {
    EVENT_UDP_RECV = 0,
    EVENT_TAP_READ,
    NUMS_OF_EVENT,
};

class SioIntf {
public:
    struct Ctx {
        OVERLAPPED ol;
        sockaddr_in6 addr;
        int addrLen;
        char buf[2048];
        WSABUF wsaBuf;
        DWORD dataLen;

        Ctx() {
            ZeroMemory(this, sizeof(Ctx));
            ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
            wsaBuf = { 2048, buf };
        };
    };

    static SioIntf* ptr();
    SioIntf();
    ~SioIntf();
    int init(SocketFd ufd, TapFd tfd);
    int tapReqRead(TapFd fd);
    Ctx* tapRead(TapFd fd);
    int tapWrite(TapFd fd, Ctx* ctx);
    int udpReqRecv(SocketFd fd);
    Ctx* udpRecv(SocketFd fd);
    int udpSend(SocketFd fd, Ctx* ctx);

    HANDLE events_[NUMS_OF_EVENT];
    Ctx udpCtxs_[2];
    Ctx tapCtxs_[2];
    unsigned udpCurCtxIdx_, tapCurCtxIdx_;
};

inline SioIntf* SioIntf::ptr()
{
    static SioIntf ins;
    return &ins;
}
