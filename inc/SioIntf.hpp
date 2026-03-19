#pragma     once
#include    "Common.hpp"
#include    "TapDev.hpp"
#include    "BsdSock.hpp"

class SioIntf {
public:
#ifdef      _WIN32
    struct Ctx {
        OVERLAPPED ol;
        sockaddr_in6 addr;
        INT addrLen;
        WSABUF wsaBuf;
        DWORD dataLen;
        BsdSock* sockPtr;
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
    Ctx* udpRecv(SockFd fd);
    int udpSend(SockFd fd, Ctx* ctx);

    /**
     * @brief TCP接收数据
     * 
     * @param ctx 包含用来接收的套接字，数据缓冲区
     * @return int 
     * @retval >=0 实际接收的字节数
     * @retval -1 发送失败
     */
    int tcpRecv(Ctx* ctx);

    /**
     * @brief TCP发送数据
     * 
     * @param ctx 包含用来发送的套接字，数据和数据长度
     * @return int
     * @retval >=0 实际发送的字节数
     * @retval -1 发送失败
     */
    int tcpSend(Ctx* ctx);

    Ctx udpSioCtx_;
    Ctx tapSioCtx_;
};
