#pragma     once
#include    "Common.hpp"
#include    "TapDev.hpp"
#include    "Socket.hpp"

enum {
    EVENT_UDP_RECV = 0,
    EVENT_TAP_READ,
    NUMS_OF_EVENT,
};

#ifdef      _WIN32
class SioIntf {
public:
    struct Ctx {
        OVERLAPPED ol;
        sockaddr_in6 addr;
        int addrLen;
        WSABUF wsaBuf;
        DWORD dataLen;
        char buf[DATA_BUF_SIZE];

        Ctx() {
            ZeroMemory(this, sizeof(Ctx));
            ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
            wsaBuf = { DATA_BUF_SIZE, buf };
        };
    };

    SioIntf();
    ~SioIntf();
    Ctx* tapRead(TapFd fd);
    int tapWrite(TapFd fd, Ctx* ctx);
    Ctx* udpRecv(SocketFd fd);
    int udpSend(SocketFd fd, Ctx* ctx);

    Ctx udpSioCtx_;
    Ctx tapSioCtx_;
};

#elif __linux__
class SioIntf {
public:
    struct Ctx {
        sockaddr_in6 addr;
        socklen_t addrLen;
        int dataLen;
        char buf[DATA_BUF_SIZE];

        Ctx() {
            memset(this, 0, sizeof(Ctx));
        };
    };

    SioIntf();
    ~SioIntf();
    Ctx* tapRead(TapFd fd);
    int tapWrite(TapFd fd, Ctx* ctx);
    Ctx* udpRecv(SocketFd fd);
    int udpSend(SocketFd fd, Ctx* ctx);

    Ctx udpSioCtx_;
    Ctx tapSioCtx_;
};

#endif
