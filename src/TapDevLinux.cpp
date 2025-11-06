#include    "TapDev.hpp"
#include    "LogMgr.hpp"

static const char* TAG = "[TapDev]";
static int tap_fd;

TapDev::TapDev(): writeErrCnt_(0), readErrCnt_(0) {
    memset(macAddress_, 0, sizeof(macAddress_));
}

TapDev::~TapDev() {
    close();
}

bool TapDev::open() {
    if (system("ip link set dev " TAP_NAME " up")) {
        if (system("ip tuntap add dev " TAP_NAME " mode tap"))
            return false;
        if (system("ip link set dev " TAP_NAME " up"))
            return false;
        if (system("ip link set dev " TAP_NAME " mtu 1418"))
            return false;
    }
    tap_fd = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (tap_fd == -1) {
        LOGE(TAG, "Can not open [/dev/net/tun].");
        return false;
    }

    ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, TAP_NAME, IFNAMSIZ);
    if (ioctl(tap_fd, TUNSETIFF, (void*)&ifr) == -1) {
        LOGE(TAG, "ioctl(TUNSETIFF) failed.");
        return false;
    }
    if (ioctl(tap_fd, SIOCGIFHWADDR, &ifr) == -1) {
        LOGE(TAG, "ioctl(SIOCGIFHWADDR) failed.");
        return false;
    }
    memcpy(macAddress_, ifr.ifr_hwaddr.sa_data, 6);

    return true;
}

bool TapDev::close() {
    ::close(tap_fd);
    // system("ip link del dev " TAP_NAME);

    return true;
}

bool TapDev::getMacAddr(uint8_t* buf, size_t bufLen) {
    if (bufLen < 6) {
        return false;
    }

    memcpy(buf, macAddress_, 6);

    return true;
}

bool TapDev::setIpv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen)
{
    std::ostringstream cidr;
    cidr << inet_ntoa(*ipv4Addr) << "/" << +netIdLen;

    std::ostringstream cmd;
    cmd << "ip addr flush dev " << TAP_NAME << " && ";
    cmd << "ip addr add " << cidr.str() << " dev " << TAP_NAME;
    if (system(cmd.str().c_str())) {
        LOGE(TAG, "Setting %s IP address to %s failed.", TAP_NAME, cidr.str().c_str());
        return false;
    }
    LOGI(TAG, "%s IP address has been set to %s.", TAP_NAME, cidr.str().c_str());

    return true;
}

ssize_t TapDev::write(const void* buf, size_t bufLen) {
    ssize_t writeBytes = ::write(tap_fd, buf, bufLen);
    if (writeBytes < bufLen) {
        LOGE(TAG, "writeBytes[%ld] is less than expected[%lu].", writeBytes, bufLen);
        ++writeErrCnt_;
    }

    return writeBytes;
}

ssize_t TapDev::read(void* buf, size_t bufLen, int timeout) {
    struct pollfd pfd = {tap_fd, POLLIN, 0};
    if (poll(&pfd, 1, timeout) <= 0) {
        return 0;
    }

    ssize_t readBytes = ::read(tap_fd, buf, bufLen);
    if (readBytes == -1) {
        LOGE(TAG, "Reading from tap device failed.");
        ++readErrCnt_;
    }

    return readBytes;
}
