#include    "BsdSock.hpp"
#include    "LogMgr.hpp"
#include    "Config.hpp"

bool BsdSock::s_isWsaInitialized_ = false;

bool BsdSock::initWsa()
{
    if (!s_isWsaInitialized_) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOGE("WSAStartup failed. {}", getSockErr());
            s_isWsaInitialized_ =  false;
        } else {
            s_isWsaInitialized_ = true;
        }
    }

    return s_isWsaInitialized_;
}



bool BsdSock::close()
{
    if (fd_ != INVALID_SOCKFD) {
        shutdown(fd_, SD_BOTH);
        closesocket(fd_);
        fd_ = INVALID_SOCKFD;
    }

    return true;
}

bool TcpSock::open()
{
    if (!initWsa())
        return false;

    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (!isFdValid()) {
        LOGE("Can not create tcp socket.[{}]", getSockErr());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE("TCP setsockopt(IPV6_V6ONLY) failed. {}", getSockErr());
            return false;
        }
    }

    /* allow reuse addr and port */ {
        int optval = 1;
        int optlevel = (SO_REUSEADDR);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE("TCP setsockopt(SO_REUSEADDR) failed. {}", getSockErr());
            return false;
        }
    }

    /* bind tcp socket */ {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&addr), sizeof(addr))) {
            LOGE("TCP can not bind to [::]:{}. {}", bindPort_, getSockErr());
            return false;
        }
    }

    /* set timeout */ {
        DWORD timeout = IO_WAIT_TIME * 1000;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout))) {
            LOGW("UDP can not setsockopt(SO_RCVTIMEO) to {} ms. {}", timeout, getSockErr());
        }
    }

    return true;
}

ssize_t TcpSock::send(const void* buf, size_t bufLen)
{
    size_t sendBytes = 0;

    while (g_cfgData.running() && sendBytes < bufLen) {
        ssize_t res = ::send(
            fd_,
            reinterpret_cast<const char*>(buf) + sendBytes,
            bufLen - sendBytes,
            0
        );
        if (res == SOCKET_ERROR) {
            LOGE("Failed to send.[{}]", getSockErr());
            break;
        }

        sendBytes += res;
    }

    return sendBytes;
}

ssize_t TcpSock::recv(void* buf, size_t bufLen)
{
    while (g_cfgData.running()) {
        ssize_t res = ::recv(
            fd_,
            reinterpret_cast<char*>(buf),
            bufLen,
            0
        );

        if (res != SOCKET_ERROR) {
            return res;
        }

        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
            return -2;
        }

        LOGE("Failed to recv.[{}]", getErrMsg(err).c_str());
        break;
    }

    return -1;
}

bool UdpSock::open()
{
    if (!initWsa())
        return false;

    fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (!isFdValid()) {
        LOGE("Can not create udp socket. {}", getSockErr());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE("UDP setsockopt(IPV6_V6ONLY) failed. {}", getSockErr());
            return false;
        }
    }

    /* bind udp socket */ {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&addr), sizeof(sockaddr_in6))) {
            LOGE("UDP can not bind to [::]:{}. {}", bindPort_, getSockErr());
            return false;
        }
    }

    // /* set timeout */ {
    //     DWORD timeout = WAIT_IO_TIME * 1000;
    //     if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout))) {
    //         LOGW("UDP can not setsockopt(SO_RCVTIMEO) to %lu s. {}", timeout, getSockErr());
    //     }
    // }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&UDP_BUF_SIZE, sizeof(UDP_BUF_SIZE))) {
            LOGW("UDP can not setsockopt(SO_RCVBUF) to {}. {}", UDP_BUF_SIZE, getSockErr());
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&UDP_BUF_SIZE, sizeof(UDP_BUF_SIZE))) {
            LOGW("UDP can not setsockopt(SO_SNDBUF) to {}. {}", UDP_BUF_SIZE, getSockErr());
        }
    }

    /* windows bug: udp socket 10054 */ {
        BOOL bEnalbeConnRestError = FALSE;
        DWORD dwBytesReturned = 0;
        if (WSAIoctl(fd_, _WSAIOW(IOC_VENDOR, 12), &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), nullptr, 0, &dwBytesReturned, nullptr, nullptr)) {
            LOGE("UDP WSAIoctl(_WSAIOW(IOC_VENDOR, 12)) failed. {}", getSockErr());
            return false;
        }
    }

    return true;
}

ssize_t UdpSock::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        LOGW("UDP sendBytes[{}] is less than expected[{}]. {}", sendBytes, bufLen, getSockErr());
    }

    return sendBytes;
}

ssize_t UdpSock::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (WSAGetLastError() != WSAETIMEDOUT) {
            LOGE("UDP receiving from UDP socket failed. {}", getSockErr());
        }
    }

    return recvBytes;
}
