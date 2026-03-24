#include "BsdSock.hpp"
#include "LogMgr.hpp"

static const char* TAG = "[Socket]";
const int udpBufferSize = 1024 * 1024 * 128;

static std::string getErrStr()
{
    return getErrMsg(errno);
}

BsdSock::BsdSock() noexcept
{
    ;
}

BsdSock::BsdSock(uint16_t port) noexcept
    : bindPort_(port)
{
    ;
}

BsdSock::BsdSock(uint16_t port, SockFd fd) noexcept
    : fd_(fd)
    , bindPort_(port)
{
    ;
}

BsdSock::BsdSock(BsdSock&& other) noexcept
    : fd_(other.fd_)
    , bindPort_(other.bindPort_)
{
    other.fd_ = INVALID_SOCKET;
}

BsdSock& BsdSock::operator=(BsdSock&& other) noexcept
{
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = INVALID_SOCKET;
        bindPort_ = other.bindPort_;
    }

    return *this;
}

BsdSock::~BsdSock()
{
    close();
}

bool BsdSock::open()
{
    // TODO: maybe for open raw socket?
    LOGT(TAG, "Why are we here?");

    return true;
}

bool BsdSock::close()
{
    if (fd_ != INVALID_SOCKET) {
        shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

TcpSock::TcpSock(uint16_t port) noexcept
    : BsdSock(port)
    , isPassive_(true)
{
    open();
}

TcpSock::TcpSock(uint16_t port, sockaddr_in6& addr) noexcept
    : BsdSock(port)
    , isPassive_(false)
    , remoteAddr_(addr)
{
    open();
}

TcpSock::TcpSock(uint16_t port, SockFd fd, sockaddr_in6& addr) noexcept
    : BsdSock(port, fd)
    , isPassive_(false)
    , remoteAddr_(addr)
{
    ;
}

TcpSock::TcpSock(TcpSock&& other) noexcept
    : BsdSock(std::move(other))
    , isPassive_(other.isPassive_)
    , remoteAddr_(other.remoteAddr_)
{
    ;
}

TcpSock& TcpSock::operator=(TcpSock&& other) noexcept
{
    if (this != &other) {
        BsdSock::operator=(std::move(other));
        remoteAddr_ = other.remoteAddr_;
    }

    return *this;
}

TcpSock::~TcpSock()
{
    ;
}

bool TcpSock::open()
{
    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        LOGE(TAG, "Can not create tcp socket.[%s]", getErrStr().c_str());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "TCP setsockopt(IPV6_V6ONLY) failed. %s", getErrStr().c_str());
            return false;
        }
    }

    /* allow reuse addr and port */ {
        int optval = 1;
        int optlevel = (SO_REUSEADDR | SO_REUSEPORT);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE(TAG, "TCP setsockopt(SO_REUSEADDR) failed. %s", getErrStr().c_str());
            return false;
        }
    }

    /* bind tcp socket */ {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&addr), sizeof(addr))) {
            LOGE(TAG, "TCP can not bind to [::]:%u. %s", bindPort_, getErrStr().c_str());
            return false;
        }
    }

    /* set timeout */ {
        timeval timeout = { IO_WAIT_TIME, 0 };
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
            LOGW(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %ld s. %s", timeout.tv_sec, getErrStr().c_str());
        }
    }

    return true;
}

bool TcpSock::connect()
{
    if (::connect(
        fd_,
        reinterpret_cast<const sockaddr *>(&remoteAddr_),
        sizeof(remoteAddr_)
    )) {
        LOGE(TAG, "Failed to connect.[%s]", getErrStr().c_str());
        return false;
    }
    isPassive_ = false;

    return true;
}

bool TcpSock::listen(int backlog)
{
    if (::listen(fd_, backlog)) {
        LOGE(TAG, "Failed to listen.[%s]", getErrStr().c_str());
        return false;
    }
    isPassive_ = true;

    return true;
}

TcpSockSPtr TcpSock::accept()
{
    TcpSockSPtr client = nullptr;
    sockaddr_in6 addr{};
    socklen_t addrLen = sizeof(addr);

    SockFd fd = ::accept(
        fd_,
        reinterpret_cast<sockaddr*>(&addr),
        &addrLen
    );
    if (fd != INVALID_SOCKET) {
        client = std::make_shared<TcpSock>(bindPort_, fd, addr);
    }

    return client;
}

ssize_t TcpSock::send(const void* buf, size_t bufLen)
{
    ssize_t sendBytes = ::send(fd_, (const char*)buf, bufLen, 0);
    if (sendBytes < bufLen) {
        LOGW(TAG, "TCP sendBytes[%ld] is less than expected[%ld]. %s", sendBytes, bufLen, getErrStr().c_str());
    }

    return sendBytes;
}

ssize_t TcpSock::recv(void* buf, size_t bufLen)
{
    ssize_t recvBytes = ::recv(fd_, (char*)buf, bufLen, 0);
    int err = errno;
    if (recvBytes == -1 && err == ECONNRESET) {
        recvBytes = 0;
    }

    return recvBytes;
}

UdpSock::UdpSock(uint16_t port) noexcept
    : BsdSock(port)
{
    open();
}

UdpSock::~UdpSock()
{
    // nothing to do
}

bool UdpSock::open()
{
    fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd_ == -1) {
        LOGE(TAG, "Can not create udp socket. %s", getErrStr().c_str());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "UDP setsockopt(IPV6_V6ONLY) failed. %s", getErrStr().c_str());
            return false;
        }
    }

    /* bind udp socket */ {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&addr), sizeof(sockaddr_in6))) {
            LOGE(TAG, "UDP can not bind to [::]:%u. %s", bindPort_, getErrStr().c_str());
            return false;
        }
    }

    // /* set timeout */ {
    //     timeval timeout = { WAIT_IO_TIME, 0 };
    //     if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
    //         LOGW(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %ld s. %s", timeout.tv_sec, getErrStr().c_str());
    //     }
    // }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGW(TAG, "UDP can not setsockopt(SO_RCVBUF) to %d. %s", udpBufferSize, getErrStr().c_str());
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGW(TAG, "UDP can not setsockopt(SO_SNDBUF) to %d. %s", udpBufferSize, getErrStr().c_str());
        }
    }

    return true;
}

ssize_t UdpSock::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        LOGW(TAG, "UDP sendBytes[%ld] is less than expected[%ld]. %s", sendBytes, bufLen, getErrStr().c_str());
    }

    return sendBytes;
}

ssize_t UdpSock::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOGE(TAG, "UDP receiving from UDP socket failed. %s", getErrStr().c_str());
        }
    }

    return recvBytes;
}
