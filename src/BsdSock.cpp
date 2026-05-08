#include    "BsdSock.hpp"

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
    other.fd_ = INVALID_SOCKFD;
}

BsdSock& BsdSock::operator=(BsdSock&& other) noexcept
{
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = INVALID_SOCKFD;
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

TcpSock::TcpSock(uint16_t port) noexcept
    : BsdSock(port)
    , isPassive_(true)
{
    if (!open()) {
        close();
    }
}

TcpSock::TcpSock(uint16_t port, const sockaddr_in6& addr) noexcept
    : BsdSock(port)
    , isPassive_(false)
    , remoteAddr_(addr)
{
    if (!open()) {
        close();
    }
}

TcpSock::TcpSock(uint16_t port, SockFd fd, const sockaddr_in6& addr) noexcept
    : BsdSock(port, fd)
    , isPassive_(true)
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
    if (remoteAddr_.sin6_port)
        LOGI(TAG, "Client[%s][%s] is offline.", IPv6_NTOP(remoteAddr_.sin6_addr).c_str(), remoteMac_.getMacStr().c_str());
}

bool TcpSock::connect()
{
    if (::connect(
        fd_,
        reinterpret_cast<const sockaddr *>(&remoteAddr_),
        sizeof(remoteAddr_)
    )) {
        LOGE(TAG, "Failed to connect.[%s]", getSockErr().c_str());
        return false;
    }
    isPassive_ = false;

    return true;
}

bool TcpSock::listen(int backlog)
{
    if (::listen(fd_, backlog)) {
        LOGE(TAG, "Failed to listen.[%s]", getSockErr().c_str());
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
    if (fd != INVALID_SOCKFD) {
        client = std::make_shared<TcpSock>(bindPort_, fd, addr);
    }

    return client;
}

UdpSock::UdpSock(uint16_t port) noexcept
    : BsdSock(port)
{
    if (!open()) {
        close();
    }
}

UdpSock::~UdpSock()
{
    ;
}