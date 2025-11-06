#include "Socket.hpp"
#include "LogMgr.hpp"

static const char* TAG = "[socket]";
const int udpBufferSize = 1024 * 1024 * 8;

UniversalSocket::UniversalSocket(): fd_(INVALID_SOCKET), bindPort_(0),
                          totalSendBytes_(0), totalRecvBytes_(0),
                          sendErrCnt_(0), recvErrCnt_(0)
{
    // nothing to do
}

TcpSocket::TcpSocket(uint16_t port): UniversalSocket(), isPassive(false)
{
    bindPort_ = port;
    memset(&remoteAddr_, 0, sizeof(sockaddr_in6));
}

TcpSocket::TcpSocket(TapLanSocket fd, sockaddr_in6 sa): UniversalSocket(), isPassive(false)
{
    fd = fd_;
    memcpy(&remoteAddr_, &sa, sizeof(sa));
}

TcpSocket::~TcpSocket()
{
    ::close(fd_);
}

bool TcpSocket::open()
{
    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        LOGE(TAG, "Can not create tcp socket.");
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "TCP setsockopt(IPV6_V6ONLY) failed.");
            return false;
        }
    }

    /* allow reuse addr and port */ {
        int optval = 1;
        int optlevel = (SO_REUSEADDR | SO_REUSEPORT);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE(TAG, "TCP setsockopt(SO_REUSEADDR) failed.");
            return false;
        }
    }

    /* bind tcp socket */ {
        sockaddr_in6 sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_addr = in6addr_any;
        sa.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&sa), sizeof(sockaddr_in6))) {
            LOGE(TAG, "TCP can not bind to [::]:%u.", bindPort_);
            return false;
        }
    }

    return true;
}

bool TcpSocket::close()
{
    if (fd_ != INVALID_SOCKET) {
        ::close(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

bool TcpSocket::connect()
{
    if (::connect(fd_, reinterpret_cast<const sockaddr *>(&remoteAddr_), sizeof(remoteAddr_))) {
        LOGE(TAG, "TCP connect failed.");
        return false;
    }

    return true;
}

bool TcpSocket::listen()
{
    if (::listen(fd_, 5)) {
        LOGE(TAG, "TCP listen failed.");
        return false;
    }
    isPassive = true;

    return true;
}

TcpSocket TcpSocket::accept()
{
    sockaddr_in6 sa;
    socklen_t saLen = sizeof(sa);
    TapLanSocket client = ::accept(fd_, reinterpret_cast<sockaddr*>(&sa), &saLen);
    if (client == INVALID_SOCKET) {
        LOGE(TAG, "TCP accept failed.");
    }

    return TcpSocket(client, sa);
}

ssize_t TcpSocket::send(const void* buf, size_t bufLen)
{
    ssize_t sendBytes = ::send(fd_, (const char*)buf, bufLen, 0);
    if (sendBytes < bufLen) {
        LOGE(TAG, "TCP sendBytes[%ld] is less than expected[%ld].", sendBytes, bufLen);
    }

    return sendBytes;
}

ssize_t TcpSocket::recv(void* buf, size_t bufLen)
{
    ssize_t recvBytes = ::recv(fd_, (char*)buf, bufLen, 0);
    if (recvBytes == -1) {
        if (errno == ECONNRESET || errno == ETIMEDOUT) {
            recvBytes = 0;
        } else {
            LOGE(TAG, "TCP receiving from TCP socket failed.");
        }
    }

    return recvBytes;
}

UdpSocket::UdpSocket(uint16_t port): UniversalSocket()
{
    bindPort_ = port;
}

UdpSocket::~UdpSocket()
{
    ::close(fd_);
}

bool UdpSocket::open()
{
    fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd_ == -1) {
        LOGE(TAG, "Can not create udp socket.");
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "UDP setsockopt(IPV6_V6ONLY) failed.");
            return false;
        }
    }

    /* bind udp socket */ {
        sockaddr_in6 sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_addr = in6addr_any;
        sa.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&sa), sizeof(sockaddr_in6))) {
            LOGE(TAG, "UDP can not bind to [::]:%u.", bindPort_);
            return false;
        }
    }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGE(TAG, "UDP can not setsockopt(SO_RCVBUF) to %d.", udpBufferSize);
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGE(TAG, "UDP can not setsockopt(SO_SNDBUF) to %d.", udpBufferSize);
        }
    }

    /* set timeout */ {
        timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout))) {
            LOGE(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %lds%ldus.", timeout.tv_sec, timeout.tv_usec);
            return false;
        }
    }

    return true;
}

bool UdpSocket::close()
{
    if (fd_ != INVALID_SOCKET) {
        ::close(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

ssize_t UdpSocket::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        LOGE(TAG, "UDP sendBytes[%ld] is less than expected[%ld].", sendBytes, bufLen);
        ++sendErrCnt_;
    }

    return sendBytes;
}

ssize_t UdpSocket::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOGE(TAG, "UDP receiving from UDP socket failed.");
            ++recvErrCnt_;
        }
    }

    return recvBytes;
}
