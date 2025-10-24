#include "Socket.hpp"
#include "LogMgr.hpp"

const char* SOCK_TAG = "[socket]";
const int udpBufferSize = 1024 * 1024 * 8;
bool UnixSocket::s_isWsaInitialized_ = false;

UnixSocket::UnixSocket(): fd_(INVALID_SOCKET), bindPort_(0),
                          totalSendBytes_(0), totalRecvBytes_(0),
                          sendErrCnt_(0), recvErrCnt_(0)
{
    // nothing to do
}

TcpSocket::TcpSocket(uint16_t port): UnixSocket(), isPassive(false)
{
    bindPort_ = port;
    memset(&remoteAddr_, 0, sizeof(sockaddr_storage));
}

TcpSocket::TcpSocket(TapLanSocket fd, sockaddr_storage sa): UnixSocket(), isPassive(false)
{
    fd = fd_;
    memcpy(&remoteAddr_, &sa, sizeof(sa));
}

TcpSocket::~TcpSocket()
{
    closesocket(fd_);
}

bool TcpSocket::open()
{
    if (!s_isWsaInitialized_) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOGE(SOCK_TAG, "WSAStartup failed. %d", WSAGetLastError());
            return false;
        } else {
            s_isWsaInitialized_ = true;
        }
    }

    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        LOGE(SOCK_TAG, "Can not create tcp socket. %d", WSAGetLastError());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(SOCK_TAG, "TCP setsockopt(IPV6_V6ONLY) failed. %d", WSAGetLastError());
            return false;
        }
    }

    /* allow reuse addr and port */ {
        int optval = 1;
        int optlevel = (SO_REUSEADDR);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE(SOCK_TAG, "TCP setsockopt(SO_REUSEADDR) failed. %d", WSAGetLastError());
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
            LOGE(SOCK_TAG, "TCP can not bind to [::]:%u. %d", bindPort_, WSAGetLastError());
            return false;
        }
    }

    return true;
}

bool TcpSocket::close()
{
    if (fd_ != INVALID_SOCKET) {
        closesocket(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

bool TcpSocket::connect()
{
    if (::connect(fd_, reinterpret_cast<const sockaddr *>(&remoteAddr_), sizeof(remoteAddr_))) {
        LOGE(SOCK_TAG, "TCP connect failed. %d", WSAGetLastError());
        return false;
    }

    return true;
}

bool TcpSocket::listen()
{
    if (::listen(fd_, 5)) {
        LOGE(SOCK_TAG, "TCP listen failed. %d", WSAGetLastError());
        return false;
    }
    isPassive = true;

    return true;
}

TcpSocket TcpSocket::accept()
{
    sockaddr_storage sa;
    int saLen = sizeof(sa);
    TapLanSocket client = ::accept(fd_, reinterpret_cast<sockaddr*>(&sa), &saLen);
    if (client == INVALID_SOCKET) {
        LOGE(SOCK_TAG, "TCP accept failed. %d", WSAGetLastError());
    }

    return TcpSocket(client, sa);
}

ssize_t TcpSocket::send(const void* buf, size_t bufLen)
{
    ssize_t sendBytes = ::send(fd_, (const char*)buf, bufLen, 0);
    if (sendBytes < bufLen) {
        LOGE(SOCK_TAG, "TCP sendBytes[%ld] is less than expected[%ld]. %d", sendBytes, bufLen, WSAGetLastError());
    }

    return sendBytes;
}

ssize_t TcpSocket::recv(void* buf, size_t bufLen)
{
    ssize_t recvBytes = ::recv(fd_, (char*)buf, bufLen, 0);
    if (recvBytes == -1) {
        int err = WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAETIMEDOUT) {
            recvBytes = 0;
        } else {
            LOGE(SOCK_TAG, "TCP receiving from TCP socket failed. %d", WSAGetLastError());
        }
    }

    return recvBytes;
}

UdpSocket::UdpSocket(uint16_t port): UnixSocket()
{
    bindPort_ = port;
}

UdpSocket::~UdpSocket()
{
    closesocket(fd_);
}

bool UdpSocket::open()
{
    if (!s_isWsaInitialized_) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOGE(SOCK_TAG, "WSAStartup failed. %d", WSAGetLastError());
            return false;
        } else {
            s_isWsaInitialized_ = true;
        }
    }

    fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd_ == -1) {
        LOGE(SOCK_TAG, "Can not create udp socket. %d", WSAGetLastError());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(SOCK_TAG, "UDP setsockopt(IPV6_V6ONLY) failed. %d", WSAGetLastError());
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
            LOGE(SOCK_TAG, "UDP can not bind to [::]:%u. %d", bindPort_, WSAGetLastError());
            return false;
        }
    }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGE(SOCK_TAG, "UDP can not setsockopt(SO_RCVBUF) to %d. %d", udpBufferSize, WSAGetLastError());
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGE(SOCK_TAG, "UDP can not setsockopt(SO_SNDBUF) to %d. %d", udpBufferSize, WSAGetLastError());
        }
    }

    /* set timeout */ {
        timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout))) {
            LOGE(SOCK_TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %lds%ldus. %d", timeout.tv_sec, timeout.tv_usec, WSAGetLastError());
            return false;
        }
    }

    /* windows bug: udp socket 10054 */ {
        BOOL bEnalbeConnRestError = FALSE;
        DWORD dwBytesReturned = 0;
        if (WSAIoctl(fd_, _WSAIOW(IOC_VENDOR, 12), &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), nullptr, 0, &dwBytesReturned, nullptr, nullptr)) {
            LOGE(SOCK_TAG, "UDP WSAIoctl(_WSAIOW(IOC_VENDOR, 12)) failed. %d", WSAGetLastError());
            return false;
        }
    }

    return true;
}

bool UdpSocket::close()
{
    if (fd_ != INVALID_SOCKET) {
        closesocket(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

ssize_t UdpSocket::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        LOGE(SOCK_TAG, "UDP sendBytes[%ld] is less than expected[%ld]. %d", sendBytes, bufLen, WSAGetLastError());
        ++sendErrCnt_;
    }

    return sendBytes;
}

ssize_t UdpSocket::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (WSAETIMEDOUT != WSAGetLastError()) {
            LOGE(SOCK_TAG, "UDP receiving from UDP socket failed. %d", WSAGetLastError());
            ++recvErrCnt_;
        }
    }

    return recvBytes;
}
