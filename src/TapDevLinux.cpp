#include    "TapDev.hpp"
#include    "LogMgr.hpp"

TapDev::TapDev()
    : fd_(INVALID_TAPFD)
    , mac_{}
    , writeBytes_(0)
    , writeErrs_(0)
    , readBytes_(0)
    , readErrs_(0)
    , tapSock_(INVALID_SOCKFD)
    , ifr_{}
{
    if (!open()) {
        close();
    }
}

TapDev::~TapDev()
{
    close();
}

bool TapDev::open()
{
    bool isExisting = (if_nametoindex(TAP_NAME) != 0);

    fd_ = ::open("/dev/net/tun", O_RDWR);
    if (!isFdValid()) {
        LOGF(TAG, "Failed to open [/dev/net/tun].[%s]", strerror(errno));
        return false;
    }

    tapSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (tapSock_ == INVALID_SOCKFD) {
        LOGF(TAG, "Failed to create socket.[%s]", strerror(errno));
        return false;
    }

    ifr_.ifr_flags = IFF_TAP | IFF_NO_PI;
    std::strncpy(ifr_.ifr_name, TAP_NAME, IFNAMSIZ);
    if (ioctl(fd_, TUNSETIFF, &ifr_)) {
        LOGF(TAG, "Failed to create TAP.[%s]", strerror(errno));
        return false;
    }

    if (isExisting) {
        if (ioctl(tapSock_, SIOCGIFHWADDR, &ifr_)) {
            LOGF(TAG, "Failed to get MAC address.[%s]", strerror(errno));
            return false;
        }
        std::memcpy(mac_.addr, ifr_.ifr_hwaddr.sa_data, 6);
    } else {
        if (ioctl(fd_, TUNSETPERSIST, 1)) {
            LOGF(TAG, "Failed to set persistent mode.[%s]", strerror(errno));
            return false;
        }

        mac_.generateMac();
        ifr_.ifr_hwaddr.sa_family = ARPHRD_ETHER;
        std::memcpy(ifr_.ifr_hwaddr.sa_data, mac_.addr, 6);
        if (ioctl(tapSock_, SIOCSIFHWADDR, &ifr_)) {
            LOGF(TAG, "Failed to set MAC address.[%s]", strerror(errno));
            return false;
        }
    }

    ifr_.ifr_mtu = TAP_MTU_SIZE;
    if (ioctl(tapSock_, SIOCSIFMTU, &ifr_)) {
        LOGF(TAG, "Failed to set MTU to [%u].[%s]", TAP_MTU_SIZE, strerror(errno));
        return false;
    }

    ifr_.ifr_qlen = TAP_QLEN;
    if (ioctl(tapSock_, SIOCSIFTXQLEN, &ifr_)) {
        LOGF(TAG, "Failed to set qlen to [%u].[%s]", TAP_QLEN, strerror(errno));
        return false;
    }

    if (ioctl(tapSock_, SIOCGIFFLAGS, &ifr_)) {
        LOGF(TAG, "Failed to get flags.[%s]", strerror(errno));
        return false;
    }
    ifr_.ifr_flags |= (IFF_UP | IFF_RUNNING);
    if (ioctl(tapSock_, SIOCSIFFLAGS, &ifr_)) {
        LOGF(TAG, "Failed to set status to up.[%s]", strerror(errno));
        return false;
    }

    return true;
}

bool TapDev::close()
{
    if (isFdValid()) {
        ::close(fd_);
        fd_ = INVALID_TAPFD;
    }
    if (tapSock_ != INVALID_SOCKFD) {
        ::close(tapSock_);
        tapSock_ = INVALID_SOCKFD;
    }
    // system("ip link del dev " TAP_NAME);

    return true;
}

bool TapDev::setIPv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen)
{
    sockaddr_in* addr = (sockaddr_in*)&ifr_.ifr_addr;
    addr->sin_family = AF_INET;

    addr->sin_addr.s_addr = ipv4Addr->s_addr;
    if (ioctl(tapSock_, SIOCSIFADDR, &ifr_)) {
        LOGE(TAG, "Failed to set IPv4 address to %s.[%s]", inet_ntoa(*ipv4Addr), strerror(errno));
        return false;
    }

    addr->sin_addr.s_addr = (netIdLen == 0) ? 0 : htonl(0xFFFFFFFFU << (32 - netIdLen));
    if (ioctl(tapSock_, SIOCSIFNETMASK, &ifr_)) {
        LOGE(TAG, "Failed to set netmask to %u.[%s]", netIdLen, strerror(errno));
        return false;
    }

    LOGI(TAG, "%s IP address set to %s/%u", TAP_NAME, inet_ntoa(*ipv4Addr), netIdLen);
    return true;
}
