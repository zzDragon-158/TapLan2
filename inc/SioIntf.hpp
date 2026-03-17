#pragma     once
#include    "Common.hpp"
#include    "TapDev.hpp"
#include    "Socket.hpp"

class SioIntf {
public:
#ifdef      _WIN32
    struct Ctx {
        OVERLAPPED ol;
        sockaddr_in6 addr;
        INT addrLen;
        WSABUF wsaBuf;
        DWORD dataLen;
        char buf[DATA_BUF_SIZE];

        Ctx() {
            ZeroMemory(this, sizeof(Ctx));
            ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
            wsaBuf = { DATA_BUF_SIZE, buf };
        };
    };

#elif __linux__
    struct Ctx {
        sockaddr_in6 addr;
        socklen_t addrLen;
        int dataLen;
        char buf[DATA_BUF_SIZE];

        Ctx() {
            memset(this, 0, sizeof(Ctx));
        };
    };

#endif

    SioIntf();
    ~SioIntf();
    Ctx* tapRead(TapFd fd);
    int tapWrite(TapFd fd, Ctx* ctx);
    Ctx* udpRecv(SocketFd fd);
    int udpSend(SocketFd fd, Ctx* ctx);

    Ctx udpSioCtx_;
    Ctx tapSioCtx_;
};
