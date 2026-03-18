#include    "TapDev.hpp"
#include    "LogMgr.hpp"

static const char* TAG = "[TapDev]";
static SocketFd tapSock = -1;
static ifreq ifr{};

TapDev::TapDev(): fdValid_(false), mac_{},
                    writeErrs_(0), readErrs_(0) {
    fdValid_ = open();
}

TapDev::~TapDev() {
    close();
}

bool TapDev::open() {
    bool isExisting = (if_nametoindex(TAP_NAME) != 0);

    fd_ = ::open("/dev/net/tun", O_RDWR);
    if (fd_ == -1) {
        LOGF(TAG, "Failed to open [/dev/net/tun].[%s]", strerror(errno));
        return false;
    }

    tapSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (tapSock == -1) {
        LOGF(TAG, "Failed to create socket.[%s]", strerror(errno));
        return false;
    }

    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, TAP_NAME, IFNAMSIZ);
    if (ioctl(fd_, TUNSETIFF, &ifr)) {
        LOGF(TAG, "Failed to create TAP.[%s]", strerror(errno));
        return false;
    }

    if (isExisting) {
        if (ioctl(tapSock, SIOCGIFHWADDR, &ifr)) {
            LOGF(TAG, "Failed to get MAC address.[%s]", strerror(errno));
            return false;
        }
        std::memcpy(mac_.addr, ifr.ifr_hwaddr.sa_data, 6);
    } else {
        if (ioctl(fd_, TUNSETPERSIST, 1)) {
            LOGF(TAG, "Failed to set persistent mode.[%s]", strerror(errno));
            return false;
        }

        mac_.generateMac();
        ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
        std::memcpy(ifr.ifr_hwaddr.sa_data, mac_.addr, 6);
        if (ioctl(tapSock, SIOCSIFHWADDR, &ifr)) {
            LOGF(TAG, "Failed to set MAC address.[%s]", strerror(errno));
            return false;
        }
    }

    ifr.ifr_mtu = TAP_MTU_SIZE;
    if (ioctl(tapSock, SIOCSIFMTU, &ifr)) {
        LOGF(TAG, "Failed to set MTU to [%u].[%s]", TAP_MTU_SIZE, strerror(errno));
        return false;
    }

    ifr.ifr_qlen = TAP_QLEN;
    if (ioctl(tapSock, SIOCSIFTXQLEN, &ifr)) {
        LOGF(TAG, "Failed to set qlen to [%u].[%s]", TAP_QLEN, strerror(errno));
        return false;
    }

    if (ioctl(tapSock, SIOCGIFFLAGS, &ifr)) {
        LOGF(TAG, "Failed to get flags.[%s]", strerror(errno));
        return false;
    }
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
    if (ioctl(tapSock, SIOCSIFFLAGS, &ifr)) {
        LOGF(TAG, "Failed to set status to up.[%s]", strerror(errno));
        return false;
    }

    return true;
}

bool TapDev::close() {
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
    if (tapSock != -1) {
        ::close(tapSock);
        tapSock = -1;
    }
    // system("ip link del dev " TAP_NAME);

    return true;
}

void TapDev::getMacAddr(Mac& mac) {
    mac = mac_;
}

bool TapDev::setIPv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen)
{
    sockaddr_in* addr = (sockaddr_in*)&ifr.ifr_addr;
    addr->sin_family = AF_INET;

    addr->sin_addr.s_addr = ipv4Addr->s_addr;
    if (ioctl(tapSock, SIOCSIFADDR, &ifr)) {
        LOGE(TAG, "Failed to set IPv4 address to %s.[%s]", inet_ntoa(*ipv4Addr), strerror(errno));
        return false;
    }

    addr->sin_addr.s_addr = (netIdLen == 0) ? 0 : htonl(0xFFFFFFFFU << (32 - netIdLen));
    if (ioctl(tapSock, SIOCSIFNETMASK, &ifr)) {
        LOGE(TAG, "Failed to set netmask to %u.[%s]", netIdLen, strerror(errno));
        return false;
    }

    LOGI(TAG, "%s IP address set to %s/%u", TAP_NAME, inet_ntoa(*ipv4Addr), netIdLen);
    return true;
}
