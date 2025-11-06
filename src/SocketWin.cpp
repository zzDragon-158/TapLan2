#include "Socket.hpp"
#include "LogMgr.hpp"

static const char* TAG = "[socket]";
const int udpBufferSize = 1024 * 1024 * 8;
bool UniversalSocket::s_isWsaInitialized_ = false;

UniversalSocket::UniversalSocket(): fd_(INVALID_SOCKET), fdValid_(false), bindPort_(0),
                          totalSendBytes_(0), totalRecvBytes_(0),
                          sendErrCnt_(0), recvErrCnt_(0)
{
    // nothing to do
}

UniversalSocket::~UniversalSocket()
{
    close();
}

bool UniversalSocket::open()
{
    // TODO: maybe for open raw socket?
    LOGT(TAG, "Why are we here?");

    return true;
}

bool UniversalSocket::close()
{
    if (fd_ != INVALID_SOCKET) {
        closesocket(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

TcpSocket::TcpSocket(uint16_t localPort): UniversalSocket(), isPassive_(true)
{
    bindPort_ = localPort;
    memset(&remoteAddr_, 0, sizeof(sockaddr_in6));
    fdValid_ = open();
}

TcpSocket::TcpSocket(sockaddr_in6 serverAddr): UniversalSocket(), isPassive_(false)
{
    memcpy(&remoteAddr_, &serverAddr, sizeof(sockaddr_in6));
    fdValid_ = open();
}

TcpSocket::TcpSocket(TapLanSocket fd, sockaddr_in6 sa): UniversalSocket(), isPassive_(false)
{
    fd_ = fd;
    memcpy(&remoteAddr_, &sa, sizeof(sa));
    fdValid_ = (fd != INVALID_SOCKET);
}

TcpSocket::~TcpSocket()
{
    // nothing to do
}

bool TcpSocket::open()
{
    if (!s_isWsaInitialized_) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOGE(TAG, "WSAStartup failed. %d", WSAGetLastError());
            return false;
        } else {
            s_isWsaInitialized_ = true;
        }
    }

    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        LOGE(TAG, "Can not create tcp socket. %d", WSAGetLastError());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "TCP setsockopt(IPV6_V6ONLY) failed. %d", WSAGetLastError());
            return false;
        }
    }

    /* allow reuse addr and port */ {
        int optval = 1;
        int optlevel = (SO_REUSEADDR);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE(TAG, "TCP setsockopt(SO_REUSEADDR) failed. %d", WSAGetLastError());
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
            LOGE(TAG, "TCP can not bind to [::]:%u. %d", bindPort_, WSAGetLastError());
            return false;
        }
    }

    return true;
}

bool TcpSocket::connect()
{
    if (::connect(fd_, reinterpret_cast<const sockaddr *>(&remoteAddr_), sizeof(remoteAddr_))) {
        LOGE(TAG, "TCP connect failed. %d", WSAGetLastError());
        return false;
    }
    isPassive_ = false;

    return true;
}

bool TcpSocket::listen(int backlog)
{
    if (::listen(fd_, backlog)) {
        LOGE(TAG, "TCP listen failed. %d", WSAGetLastError());
        return false;
    }
    isPassive_ = true;

    return true;
}

bool TcpSocket::accept(TapLanSocket& fd, sockaddr_in6& addr)
{
    socklen_t addrLen = sizeof(sockaddr_in6);
    fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    if (fd == INVALID_SOCKET) {
        return false;
    }

    return true;
}

ssize_t TcpSocket::send(const void* buf, size_t bufLen)
{
    ssize_t sendBytes = ::send(fd_, (const char*)buf, bufLen, 0);
    if (sendBytes < bufLen) {
        LOGE(TAG, "TCP sendBytes[%ld] is less than expected[%ld]. %d", sendBytes, bufLen, WSAGetLastError());
    }

    return sendBytes;
}

ssize_t TcpSocket::recv(void* buf, size_t bufLen, int timeout)
{
    if (timeout >= 0) {
        TapLanPollFd pfd = { fd_, POLLIN, 0 };
        int pollCnt = TapLanPoll(&pfd, 1, timeout);
        if (pollCnt <= 0) {
            if (pollCnt == -1) {
                LOGE(TAG, "Poll failed.");
            }
            return pollCnt;
        }
    }

    ssize_t recvBytes = ::recv(fd_, (char*)buf, bufLen, 0);
    if (recvBytes == -1) {
        int err = WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAETIMEDOUT) {
            recvBytes = 0;
        } else {
            LOGE(TAG, "TCP receiving from TCP socket failed. %d", WSAGetLastError());
        }
    }

    return recvBytes;
}

// bool TcpSocket::recv(CbRecvFunc& cbRecv)
// {
//     uint8_t recvBuf[65536];
//     uint8_t sendBuf[65536];
//     int pollCnt = TapLanPoll(pfds_.data(), pfds_.size(), 5000);
//     if (pollCnt < 0) {
//         LOGE(TAG, "TapLanPoll failed. %d", WSAGetLastError());
//         return false;
//     }

//     size_t pfdsLen = pfds_.size();
//     if (pfds_[0].revents != 0) {
//         --pollCnt;
//         if (!accept()) {
//             LOGE(TAG, "TCP accept failed. %d", WSAGetLastError());
//         }
//     }
//     size_t offset = 0;
//     for (size_t i = pfdsLen - 1; i > 0 && pollCnt; --pfdsLen) {
//         if (pfds_[i].revents != 0) {
//             --pollCnt;
//             TcpSocket& client = clients_[i - 1];
//             ssize_t recvBytes = client.recv(recvBuf, sizeof(recvBuf));
//             if (recvBytes == 0) {
//                 // TODO: need set node status
//                 pfds_.erase(pfds_.begin() + i);
//                 clients_.erase(clients_.begin() + i);
//                 continue;
//             } else if (recvBytes == -1) {
//                 continue;
//             }
//             size_t sendBytes = sizeof(sendBuf);
//             cbRecv(recvBuf, recvBytes, reinterpret_cast<const sockaddr_in6*>(&client.remoteAddr_), sendBuf, sendBytes);
//             client.send(sendBuf, sendBytes);
//         }
//     }

//     return true;
// }

void TcpSocket::getRemoteAddr(sockaddr_in6* addr)
{
    memcpy(addr, &remoteAddr_, sizeof(sockaddr_in6));
}

UdpSocket::UdpSocket(uint16_t port): UniversalSocket()
{
    bindPort_ = port;
    fdValid_ = open();
}

UdpSocket::~UdpSocket()
{
    // nothing to do
}

bool UdpSocket::open()
{
    if (!s_isWsaInitialized_) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOGE(TAG, "WSAStartup failed. %d", WSAGetLastError());
            return false;
        } else {
            s_isWsaInitialized_ = true;
        }
    }

    fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd_ == -1) {
        LOGE(TAG, "Can not create udp socket. %d", WSAGetLastError());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "UDP setsockopt(IPV6_V6ONLY) failed. %d", WSAGetLastError());
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
            LOGE(TAG, "UDP can not bind to [::]:%u. %d", bindPort_, WSAGetLastError());
            return false;
        }
    }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGE(TAG, "UDP can not setsockopt(SO_RCVBUF) to %d. %d", udpBufferSize, WSAGetLastError());
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGE(TAG, "UDP can not setsockopt(SO_SNDBUF) to %d. %d", udpBufferSize, WSAGetLastError());
        }
    }

    // /* set timeout */ {
    //     timeval timeout;
    //     timeout.tv_sec = 5;
    //     timeout.tv_usec = 0;
    //     if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout))) {
    //         LOGE(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %lds%ldus. %d", timeout.tv_sec, timeout.tv_usec, WSAGetLastError());
    //         return false;
    //     }
    // }

    /* windows bug: udp socket 10054 */ {
        BOOL bEnalbeConnRestError = FALSE;
        DWORD dwBytesReturned = 0;
        if (WSAIoctl(fd_, _WSAIOW(IOC_VENDOR, 12), &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), nullptr, 0, &dwBytesReturned, nullptr, nullptr)) {
            LOGE(TAG, "UDP WSAIoctl(_WSAIOW(IOC_VENDOR, 12)) failed. %d", WSAGetLastError());
            return false;
        }
    }

    return true;
}

ssize_t UdpSocket::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        LOGE(TAG, "UDP sendBytes[%ld] is less than expected[%ld]. %d", sendBytes, bufLen, WSAGetLastError());
        ++sendErrCnt_;
    }

    return sendBytes;
}

ssize_t UdpSocket::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (WSAETIMEDOUT != WSAGetLastError()) {
            LOGE(TAG, "UDP receiving from UDP socket failed. %d", WSAGetLastError());
            ++recvErrCnt_;
        }
    }

    return recvBytes;
}
