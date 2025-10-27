#pragma     once
#include    <cstdint>
#include    <vector>

#ifdef      _WIN32
// #include    <WS2tcpip.h>
#include    "WinHeaders.hpp"

#define     TapLanPoll          WSAPoll

typedef SOCKET TapLanSocket;

#elif __linux__
#include    <poll.h>            // for poll
#include    <unistd.h>          // for close
#include    <cstring>           // for memset
#include    <cerrno>            // for errno
#include    <sys/socket.h>      // for socket
#include    <arpa/inet.h>       // for in6addr_any

#define     TapLanPoll          poll
#define     INVALID_SOCKET      -1

typedef int TapLanSocket;

#else
#error      "unsupported platform!"

#endif

typedef pollfd TapLanPollFD;

class UnixSocket {
public:
    UnixSocket();
protected:
    TapLanSocket fd_;
    uint16_t bindPort_;
    uint64_t totalSendBytes_;
    uint64_t totalRecvBytes_;
    uint64_t sendErrCnt_;
    uint64_t recvErrCnt_;
#ifdef _WIN32
    static bool s_isWsaInitialized_;
#endif
};

class TcpSocket: public UnixSocket {
public:
    TcpSocket(uint16_t port);
    TcpSocket(TapLanSocket fd, sockaddr_storage remote);    // for accept
    ~TcpSocket();
    bool open();
    bool close();
    bool connect();
    bool listen();
    TcpSocket accept();
    ssize_t send(const void* buf, size_t bufLen);
    ssize_t recv(void* buf, size_t bufLen);

private:
    bool isPassive;
    sockaddr_storage remoteAddr_;
    std::vector<TapLanPollFD> pfd_;
};

class UdpSocket: public UnixSocket {
public:
    UdpSocket(uint16_t port);
    ~UdpSocket();
    bool open();
    bool close();
    ssize_t sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen);
    ssize_t recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen);
};
