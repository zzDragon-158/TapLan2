#include "Socket.hpp"
#include "LogMgr.hpp"

static const char* TAG = "[Socket]";
const int udpBufferSize = 1024 * 1024 * 8;
bool UniversalSocket::s_isWsaInitialized_ = false;

UniversalSocket::UniversalSocket(): fd_(INVALID_SOCKET), fdValid_(false), bindPort_(0),
                          sendBytes_(0), recvBytes_(0),
                          sendErrors_(0), recvErrors_(0)
{
    // nothing to do
}

UniversalSocket::UniversalSocket(UniversalSocket&& other) noexcept: fd_(other.fd_), fdValid_(other.fdValid_), bindPort_(other.bindPort_),
                                                                    sendBytes_(other.sendBytes_), recvBytes_(other.recvBytes_),
                                                                    sendErrors_(other.sendErrors_), recvErrors_(other.sendErrors_)
{
    other.fd_ = INVALID_SOCKET;
}

UniversalSocket::~UniversalSocket()
{
    close();
}

bool UniversalSocket::initWsa()
{
    if (!s_isWsaInitialized_) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOGE(TAG, "WSAStartup failed. %s", getErrStr().c_str());
            s_isWsaInitialized_ =  false;
        } else {
            s_isWsaInitialized_ = true;
        }
    }

    return s_isWsaInitialized_;
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
        LOGT(TAG, "close fd_[%ld]", fd_);
        closesocket(fd_);
        fd_ = INVALID_SOCKET;
    }

    return true;
}

std::string UniversalSocket::getErrStr()
{
    int errorCode = WSAGetLastError();
    LPTSTR errorBuffer = nullptr;
    size_t size = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        // MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),   // local language chinese
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),   // english
        (LPTSTR)&errorBuffer,
        0,
        NULL
    );
    if (size == 0) {
        return "Unknown error or FormatMessage failed (Code: " + std::to_string(errorCode) + ")";
    }

    std::string errorString(errorBuffer);
    LocalFree(errorBuffer);
    while (!errorString.empty() && (errorString.back() == '\r' || errorString.back() == '\n')) {
        errorString.pop_back();
    }

    return "[Error " + std::to_string(errorCode) + "] " + errorString;
}

UniversalSocket& UniversalSocket::operator=(UniversalSocket&& other) noexcept
{
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }

    return *this;
}

TcpSocket::TcpSocket(uint16_t localPort): UniversalSocket(), isPassive_(true)
{
    bindPort_ = localPort;
    memset(&remoteAddr_, 0, sizeof(sockaddr_in6));
    fdValid_ = open();
}

TcpSocket::TcpSocket(uint16_t localPort, sockaddr_in6 serverAddr): UniversalSocket(), isPassive_(false)
{
    bindPort_ = localPort;
    memcpy(&remoteAddr_, &serverAddr, sizeof(sockaddr_in6));
    fdValid_ = open();
}

TcpSocket::TcpSocket(TapLanSocket fd, sockaddr_in6 sa): UniversalSocket(), isPassive_(false)
{
    fd_ = fd;
    memcpy(&remoteAddr_, &sa, sizeof(sa));
    fdValid_ = (fd != INVALID_SOCKET);
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept: UniversalSocket(std::move(other)), isPassive_(other.isPassive_)
{
    LOGT(TAG, "move construction fd_[%ld]", fd_);
    memcpy(&remoteAddr_, &other.remoteAddr_, sizeof(sockaddr_in6));
}

TcpSocket::~TcpSocket()
{
    // nothing to do
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other) {
        UniversalSocket::operator=(std::move(other));
        memcpy(&remoteAddr_, &other.remoteAddr_, sizeof(sockaddr_in6));
        memset(&other.remoteAddr_, 0, sizeof(sockaddr_in6));
    }

    return *this;
}

bool TcpSocket::open()
{
    if (!initWsa())
        return false;

    fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET) {
        LOGE(TAG, "Can not create tcp socket. %s", getErrStr().c_str());
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
        int optlevel = (SO_REUSEADDR);
        if (setsockopt(fd_, SOL_SOCKET, optlevel, (char*)&optval, sizeof(optval))) {
            LOGE(TAG, "TCP setsockopt(SO_REUSEADDR) failed. %s", getErrStr().c_str());
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
            LOGE(TAG, "TCP can not bind to [::]:%u. %s", bindPort_, getErrStr().c_str());
            return false;
        }
    }

    return true;
}

bool TcpSocket::connect()
{
    if (::connect(fd_, reinterpret_cast<const sockaddr *>(&remoteAddr_), sizeof(remoteAddr_))) {
        LOGE(TAG, "TCP connect failed. %s", strerror(WSAGetLastError()));
        return false;
    }
    isPassive_ = false;

    return true;
}

bool TcpSocket::listen(int backlog)
{
    if (::listen(fd_, backlog)) {
        LOGE(TAG, "TCP listen failed. %s", getErrStr().c_str());
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
        LOGW(TAG, "Accept connection failed. %s", getErrStr().c_str());
        return false;
    }

    return true;
}

ssize_t TcpSocket::send(const void* buf, size_t bufLen)
{
    ssize_t sendBytes = ::send(fd_, (const char*)buf, bufLen, 0);
    if (sendBytes < bufLen) {
        ++sendErrors_;
        LOGW(TAG, "TCP sendBytes[%ld] is less than expected[%ld]. %s", sendBytes, bufLen, getErrStr().c_str());
    }

    sendBytes_ += sendBytes;
    return sendBytes;
}

ssize_t TcpSocket::recv(void* buf, size_t bufLen, int timeout)
{
    if (timeout >= 0) {
        TapLanPollFd pfd = { fd_, POLLIN, 0 };
        int pollCnt = TapLanPoll(&pfd, 1, timeout);
        if (pollCnt == 0) {
            return -1;
        } else if (pollCnt == -1) {
            ++recvErrors_;
            LOGW(TAG, "TCP poll failed. %s", getErrStr().c_str());
            return -1;
        }
    }

    ssize_t recvBytes = ::recv(fd_, (char*)buf, bufLen, 0);
    if (recvBytes == -1) {
        if (errno != WSAECONNRESET && errno != WSAETIMEDOUT) {
            ++recvErrors_;
            LOGE(TAG, "TCP receiving from TCP socket[%ld] failed. %s", fd_, getErrStr().c_str());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else if (errno == WSAECONNRESET) {
            recvBytes = 0;
        }
    }

    recvBytes_ += recvBytes;
    return recvBytes;
}

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
    if (!initWsa())
        return false;

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
        sockaddr_in6 sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_addr = in6addr_any;
        sa.sin6_port = htons(bindPort_);
        if (bind(fd_, (sockaddr*)(&sa), sizeof(sockaddr_in6))) {
            LOGE(TAG, "UDP can not bind to [::]:%u. %s", bindPort_, getErrStr().c_str());
            return false;
        }
    }

    /* set timeout */ {
        DWORD timeoutMs = 3000;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeoutMs, sizeof(timeoutMs))) {
            LOGW(TAG, "UDP can not setsockopt(SO_RCVTIMEO) to %lu ms. %s", timeoutMs, getErrStr().c_str());
        }
    }

    /* set udp buffer size */ {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGW(TAG, "UDP can not setsockopt(SO_RCVBUF) to %d. %s", udpBufferSize, getErrStr().c_str());
        }
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (char*)&udpBufferSize, sizeof(udpBufferSize))) {
            LOGW(TAG, "UDP can not setsockopt(SO_SNDBUF) to %d. %s", udpBufferSize, getErrStr().c_str());
        }
    }

    /* windows bug: udp socket 10054 */ {
        BOOL bEnalbeConnRestError = FALSE;
        DWORD dwBytesReturned = 0;
        if (WSAIoctl(fd_, _WSAIOW(IOC_VENDOR, 12), &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), nullptr, 0, &dwBytesReturned, nullptr, nullptr)) {
            LOGE(TAG, "UDP WSAIoctl(_WSAIOW(IOC_VENDOR, 12)) failed. %s", getErrStr().c_str());
            return false;
        }
    }

    return true;
}

ssize_t UdpSocket::sendTo(const void* buf, size_t bufLen, const sockaddr* dstAddr, socklen_t addrLen)
{
    ssize_t sendBytes = sendto(fd_, (const char*)buf, bufLen, 0, dstAddr, addrLen);
    if (sendBytes < bufLen) {
        ++sendErrors_;
        LOGW(TAG, "UDP sendBytes[%ld] is less than expected[%ld]. %d", sendBytes, bufLen, getErrStr().c_str());
    }

    sendBytes_ += sendBytes;
    return sendBytes;
}

ssize_t UdpSocket::recvFrom(void* buf, size_t bufLen, sockaddr* srcAddr, socklen_t* addrLen)
{
    ssize_t recvBytes = recvfrom(fd_, (char*)buf, bufLen, 0, srcAddr, addrLen);
    if (recvBytes == -1) {
        if (WSAGetLastError() != WSAETIMEDOUT) {
            ++recvErrors_;
            LOGE(TAG, "UDP receiving from UDP socket failed. %s", getErrStr().c_str());
        }
    } else {
        recvBytes_ += recvBytes;
    }

    return recvBytes;
}
