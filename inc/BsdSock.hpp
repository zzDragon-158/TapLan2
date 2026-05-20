#pragma     once
#include    <cstdint>
#include    <memory>
#include    "Common.hpp"

class BsdSock {
public:
    BsdSock() noexcept;
    BsdSock(uint16_t port) noexcept;
    BsdSock(uint16_t port, SockFd fd) noexcept;
    BsdSock(BsdSock&& other) noexcept;
    BsdSock(const BsdSock&) = delete;
    BsdSock& operator=(const BsdSock&) = delete;
    BsdSock& operator=(BsdSock&& other) noexcept;
    explicit operator SockFd() { return fd_; };
    virtual ~BsdSock();
    bool close();
    uint16_t getBindPort() { return bindPort_; };
    SockFd getFd() { return fd_; };
    bool isFdValid() { return (fd_ != INVALID_SOCKFD); };

protected:
    static constexpr const char* TAG = "[BsdSock]";
    uint16_t bindPort_ = 0;
    SockFd fd_ = INVALID_SOCKFD;
#ifdef _WIN32
    static bool s_isWsaInitialized_;

    bool initWsa();
#endif

    virtual bool open();
};

class TcpSock: public BsdSock {
public:
    TcpSock(uint16_t port) noexcept;                                     // for listen
    TcpSock(uint16_t port, const sockaddr_in6& addr) noexcept;                 // for connect
    TcpSock(uint16_t port, SockFd fd, const sockaddr_in6& addr) noexcept;      // for accept
    TcpSock(TcpSock&& other) noexcept;
    ~TcpSock();
    TcpSock& operator=(TcpSock&& other) noexcept;
    sockaddr_in6& getRemoteAddr() { return remoteAddr_; };
    Mac& getMac() { return remoteMac_; };
    void setMac(uint64_t macNum) { remoteMac_ = macNum; };
    bool connect();
    bool listen(int backlog);
    std::shared_ptr<TcpSock> accept();
    ssize_t send(const void* buf, size_t bufLen);
    ssize_t recv(void* buf, size_t bufLen);

private:
    bool isPassive_;
    sockaddr_in6 remoteAddr_{};
    Mac remoteMac_{};

    bool open();
};
using TcpSockSPtr = std::shared_ptr<TcpSock>;

class UdpSock: public BsdSock {
public:
    UdpSock(uint16_t port) noexcept;
    ~UdpSock();
    ssize_t sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen);
    ssize_t recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen);

private:
    const int UDP_BUF_SIZE = 1024 * 1024 * 128;

    bool open();
};
