#include    "TapDev.hpp"
#include    "LogMgr.hpp"

static const char* TAG = "[TapDev]";
TapFd tapFd = -1;
int tap_sock = -1;
ifreq ifr{};

TapDev::TapDev(): fdValid_(false), mac_{},
                    writeErrs_(0), readErrs_(0) {
    generateMac();
    fdValid_ = open();
}

TapDev::~TapDev() {
    close();
}

void TapDev::generateMac() {
    mac_.addr[0] = 0x02;
    mac_.addr[1] = 0x34;
    mac_.addr[2] = 0x60;

    auto now = std::chrono::high_resolution_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    uint32_t seed = static_cast<uint32_t>(micros ^ (getpid() << 16));
    mac_.addr[3] = (seed >> 16) & 0xFF;
    mac_.addr[4] = (seed >> 8) & 0xFF;
    mac_.addr[5] = seed & 0xFF;
}

bool TapDev::open() {
    tapFd = ::open("/dev/net/tun", O_RDWR);
    if (tapFd == -1) {
        LOGF(TAG, "Failed to open [/dev/net/tun].[%s]", strerror(errno));
        return false;
    }

    tap_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (tap_sock == -1) {
        LOGF(TAG, "Failed to create socket.[%s]", strerror(errno));
        return false;
    }

    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, TAP_NAME, IFNAMSIZ);
    if (ioctl(tapFd, TUNSETIFF, &ifr)) {
        LOGF(TAG, "Failed to create TAP.[%s]", strerror(errno));
        return false;
    }

    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    std::memcpy(ifr.ifr_hwaddr.sa_data, mac_.addr, 6);
    if (ioctl(tap_sock, SIOCSIFHWADDR, &ifr)) {
        LOGF(TAG, "Failed to set MAC address.[%s]", strerror(errno));
        return false;
    }

    ifr.ifr_mtu = TAP_MTU_SIZE;
    if (ioctl(tap_sock, SIOCSIFMTU, &ifr)) {
        LOGF(TAG, "Failed to set MTU to [%u].[%s]", TAP_MTU_SIZE, strerror(errno));
        return false;
    }

    if (ioctl(tap_sock, SIOCGIFFLAGS, &ifr)) {
        LOGF(TAG, "Failed to get flags.[%s]", strerror(errno));
        return false;
    }
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
    if (ioctl(tap_sock, SIOCSIFFLAGS, &ifr)) {
        LOGF(TAG, "Failed to set status to up.[%s]", strerror(errno));
        return false;
    }

    return true;
}

bool TapDev::close() {
    if (tapFd != -1)
        ::close(tapFd);
    if (tap_sock != -1)
        ::close(tap_sock);
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
    if (ioctl(tap_sock, SIOCSIFADDR, &ifr)) {
        LOGE(TAG, "Failed to set IPv4 address to %s.[%s]", inet_ntoa(*ipv4Addr), strerror(errno));
        return false;
    }

    addr->sin_addr.s_addr = (netIdLen == 0) ? 0 : htonl(0xFFFFFFFFU << (32 - netIdLen));
    if (ioctl(tap_sock, SIOCSIFNETMASK, &ifr)) {
        LOGE(TAG, "Failed to set netmask to %u.[%s]", netIdLen, strerror(errno));
        return false;
    }

    LOGI(TAG, "%s IP address set to %s/%u", TAP_NAME, inet_ntoa(*ipv4Addr), netIdLen);
    return true;
}

ssize_t TapDev::write(const void* buf, size_t bufLen) {
    ssize_t writeBytes = ::write(tapFd, buf, bufLen);
    if (writeBytes == -1) {
        LOGE(TAG, "Failed to write to TAP.[%s]", strerror(errno));
        ++writeErrs_;
        return -1;
    } else if (writeBytes < bufLen) {
        LOGW(TAG, "Actual written [%lu]bytes are less than expected written [%lu]bytes.", writeBytes, bufLen);
    }

    writeBytes_ += writeBytes;
    return writeBytes;
}

ssize_t TapDev::read(void* buf, size_t bufLen, int timeout) {
    struct pollfd pfd = {tapFd, POLLIN, 0};
    if (poll(&pfd, 1, timeout) <= 0) {
        return 0;
    }

    ssize_t readBytes = ::read(tapFd, buf, bufLen);
    if (readBytes == -1) {
        LOGE(TAG, "Failed to read from TAP.[%s]", strerror(errno));
        ++readErrs_;
        return -1;
    }

    readBytes_ += readBytes;
    return readBytes;
}
