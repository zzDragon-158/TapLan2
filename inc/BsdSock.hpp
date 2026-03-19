#pragma     once
#include    <cstdint>
#include    <string>
#include    "Common.hpp"

#ifdef      _WIN32

#define     TapLanPoll          WSAPoll

#elif __linux__
#include    <poll.h>            // for poll
#include    <unistd.h>          // for close
#include    <cstring>           // for memset
#include    <cerrno>            // for errno
#include    <cstring>           // for strerror
#include    <sys/socket.h>      // for socket
#include    <arpa/inet.h>       // for in6addr_any
#include    <liburing.h>        // for io_uring
#include    <sys/mman.h>        // for mmap

#define     TapLanPoll          poll

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

class BsdSock {
public:
    BsdSock();
    BsdSock(BsdSock&& other) noexcept;
    BsdSock(const BsdSock&) = delete;
    BsdSock& operator=(const BsdSock&) = delete;
    BsdSock& operator=(BsdSock&& other) noexcept;
    explicit operator SockFd() { return fd_; };
    ~BsdSock();
    bool isFdValid() { return (fd_ != INVALID_SOCKET); };
    bool close();
    uint16_t getBindPort() { return bindPort_; };
    uint64_t getSendBytes() { return sendBytes_; };
    uint64_t getSendErrors() { return sendErrs_; };
    uint64_t getRecvBytes() { return recvBytes_; };
    uint64_t getRecvErrors() { return recvErrs_; };
    uint64_t getDropped() { return dropped_; };
    void incSendBytes(uint64_t v) { sendBytes_ += v; };
    void incSendErrs(uint64_t v) { sendErrs_ += v; };
    void incRecvBytes(uint64_t v) { recvBytes_ += v; };
    void incRecvErrs(uint64_t v) { recvErrs_ += v; };
    void incDropped(uint64_t v) { dropped_ += v; };

protected:
    SockFd fd_;
    bool fdValid_;
    uint16_t bindPort_;
    uint64_t sendBytes_;
    uint64_t recvBytes_;
    uint64_t sendErrs_;
    uint64_t recvErrs_;
    uint64_t dropped_;
#ifdef _WIN32
    static bool s_isWsaInitialized_;

    bool initWsa();
#endif

    virtual bool open();
    std::string getErrStr();
};

class TcpSock: public BsdSock {
public:
    // typedef std::function<bool(const void* reqBuf, const size_t& reqLen, const sockaddr_in6* addr, SockFd sock, void* respBuf, size_t& respLen)> CbRecvFunc;
    TcpSock(uint16_t localPort);                              // for listen
    TcpSock(uint16_t localPort, sockaddr_in6 serverAddr);     // for connect
    TcpSock(SockFd fd, sockaddr_in6 remoteAddr);            // for accept
    TcpSock(TcpSock&& other) noexcept;
    ~TcpSock();
    TcpSock& operator=(TcpSock&& other) noexcept;
    bool connect();
    bool listen(int backlog);
    bool accept(SockFd& fd, sockaddr_in6& addr);
    ssize_t send(const void* buf, size_t bufLen);
    ssize_t recv(void* buf, size_t bufLen);
    // bool recv(CbRecvFunc& cbRecv);
    void getRemoteAddr(sockaddr_in6* addr);
    uint64_t getMac() { return remoteMac_; };
    void setMac(uint64_t macNum) { remoteMac_ = macNum; };

private:
    bool isPassive_;
    sockaddr_in6 remoteAddr_;
    Mac remoteMac_;

    bool open();
};

class UdpSock: public BsdSock {
public:
    UdpSock(uint16_t port);
    ~UdpSock();
    ssize_t sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen);
    ssize_t recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen);

private:
    bool open();
};
