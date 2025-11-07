#pragma     once
#include    <cstdint>
#include    <list>
#include    <functional>
#include    <string>

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

typedef pollfd TapLanPollFd;

inline std::string IPv4_NTOP(const in_addr& ipv4addr) {
    char ipv4str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ipv4addr, ipv4str, INET_ADDRSTRLEN);
    return std::string(ipv4str);
}

inline std::string IPv6_NTOP(const in6_addr& ipv6addr) {
    char ipv6str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &ipv6addr, ipv6str, INET6_ADDRSTRLEN);
    return std::string(ipv6str);
}

class UniversalSocket {
public:
    UniversalSocket();
    UniversalSocket(UniversalSocket&& other) noexcept;
    ~UniversalSocket();
    bool isFdValid() { return fdValid_; };
    UniversalSocket(const UniversalSocket&) = delete;
    UniversalSocket& operator=(const UniversalSocket&) = delete;
    UniversalSocket& operator=(UniversalSocket&& other) noexcept;
    explicit operator TapLanSocket() { return fd_; };
protected:
    TapLanSocket fd_;
    bool fdValid_;
    uint16_t bindPort_;
    uint64_t totalSendBytes_;
    uint64_t totalRecvBytes_;
    uint64_t sendErrCnt_;
    uint64_t recvErrCnt_;
#ifdef _WIN32
    static bool s_isWsaInitialized_;
#endif

    virtual bool open();
    bool close();
};

class TcpSocket: public UniversalSocket {
public:
    // typedef std::function<bool(const void* reqBuf, const size_t& reqLen, const sockaddr_in6* addr, TapLanSocket sock, void* respBuf, size_t& respLen)> CbRecvFunc;
    TcpSocket(uint16_t localPort);                              // for listen
    TcpSocket(uint16_t localPort, sockaddr_in6 serverAddr);     // for connect
    TcpSocket(TapLanSocket fd, sockaddr_in6 remoteAddr);        // for accept
    TcpSocket(TcpSocket&& other) noexcept;
    ~TcpSocket();
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    bool connect();
    bool listen(int backlog);
    bool accept(TapLanSocket& fd, sockaddr_in6& addr);
    ssize_t send(const void* buf, size_t bufLen);
    ssize_t recv(void* buf, size_t bufLen, int timeout = -1);
    // bool recv(CbRecvFunc& cbRecv);
    void getRemoteAddr(sockaddr_in6* addr);

private:
    bool isPassive_;
    sockaddr_in6 remoteAddr_;

    bool open();
};

class UdpSocket: public UniversalSocket {
public:
    UdpSocket(uint16_t port);
    ~UdpSocket();
    ssize_t sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen);
    ssize_t recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen);

private:
    bool open();
};
