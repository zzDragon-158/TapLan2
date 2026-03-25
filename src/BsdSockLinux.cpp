#include "BsdSock.hpp"

bool BsdSock::close()
{
    if (fd_ != INVALID_SOCKET) {
        shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

bool TcpSock::open()
{
    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        LOGE(TAG, "Can not create tcp socket.[%s]", getSockErr().c_str());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "TCP setsockopt(IPV6_V6ONLY) failed. %s", getSockErr().c_str());
            return false;
        }
    }

    /* allow reuse addr and port */ {
        int optval = 1;
        int optlevel = (SO_REUSEADDR | SO_REUSEPORT);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE(TAG, "TCP setsockopt(SO_REUSEADDR) failed. %s", getSockErr().c_str());
            return false;
        }
    }

    /* bind tcp socket */ {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&addr), sizeof(addr))) {
            LOGE(TAG, "TCP can not bind to [::]:%u. %s", bindPort_, getSockErr().c_str());
            return false;
        }
    }

    /* set timeout */ {
        timeval timeout = { IO_WAIT_TIME, 0 };
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
            LOGW(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %ld s. %s", timeout.tv_sec, getSockErr().c_str());
        }
    }

    return true;
}

ssize_t TcpSock::send(const void* buf, size_t bufLen)
{
    size_t sendBytes = 0;

    while (g_cfgData.isRunning && sendBytes < bufLen) {
        ssize_t res = ::send(
            fd_,
            reinterpret_cast<const char*>(buf) + sendBytes,
            bufLen - sendBytes,
            0
        );
        if (res == -1) {
            LOGE(TAG, "Failed to send.[%s]", getSockErr().c_str());
            break;
        }

        sendBytes += res;
    }

    return sendBytes;
}

ssize_t TcpSock::recv(void* buf, size_t bufLen)
{
    while (g_cfgData.isRunning) {
        ssize_t res = ::recv(
            fd_,
            reinterpret_cast<char*>(buf),
            bufLen,
            0
        );

        if (res != -1) {
            return res;
        }

        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return -2;
        }

        LOGE(TAG, "Failed to recv.[%s]", getErrMsg(err).c_str());
        break;
    }

    return -1;
}

bool UdpSock::open()
{
    fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd_ == -1) {
        LOGE(TAG, "Can not create udp socket. %s", getSockErr().c_str());
        return false;
    }

    /* listen to ipv4 and ipv6 */ {
        int off = 0;
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&off, sizeof(off))) {
            LOGE(TAG, "UDP setsockopt(IPV6_V6ONLY) failed. %s", getSockErr().c_str());
            return false;
        }
    }

    /* bind udp socket */ {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&addr), sizeof(sockaddr_in6))) {
            LOGE(TAG, "UDP can not bind to [::]:%u. %s", bindPort_, getSockErr().c_str());
            return false;
        }
    }

    // /* set timeout */ {
    //     timeval timeout = { WAIT_IO_TIME, 0 };
    //     if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
    //         LOGW(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %ld s. %s", timeout.tv_sec, getSockErr().c_str());
    //     }
    // }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGW(TAG, "UDP can not setsockopt(SO_RCVBUF) to %d. %s", udpBufferSize, getSockErr().c_str());
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGW(TAG, "UDP can not setsockopt(SO_SNDBUF) to %d. %s", udpBufferSize, getSockErr().c_str());
        }
    }

    return true;
}

ssize_t UdpSock::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        LOGW(TAG, "UDP sendBytes[%ld] is less than expected[%ld]. %s", sendBytes, bufLen, getSockErr().c_str());
    }

    return sendBytes;
}

ssize_t UdpSock::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOGE(TAG, "UDP receiving from UDP socket failed. %s", getSockErr().c_str());
        }
    }

    return recvBytes;
}
