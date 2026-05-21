#include    <unistd.h>          // for close
#include    <fcntl.h>           // for open, O_RDWR
#include    <cstring>           // for strncpy
#include    <sys/ioctl.h>       // for ioctl, TUNSETIFF
#include    <net/if_arp.h>      // for ARPHRD_ETHER
#include    <linux/if_tun.h>    // for IFF_TAP, IFF_NO_PI;
#include    "TapDev.hpp"
#include    "LogMgr.hpp"

TapDev::TapDev()
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

    fd_ = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (!isFdValid()) {
        LOGF("Failed to open [/dev/net/tun].[{}]", getTapErrMsg());
        return false;
    }

    tapSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (tapSock_ == INVALID_SOCKFD) {
        LOGF("Failed to create socket.[{}]", getTapErrMsg());
        return false;
    }

    ifr_.ifr_flags = IFF_TAP | IFF_NO_PI;
    std::strncpy(ifr_.ifr_name, TAP_NAME, IFNAMSIZ);
    if (ioctl(fd_, TUNSETIFF, &ifr_)) {
        LOGF("Failed to create TAP.[{}]", getTapErrMsg());
        return false;
    }

    if (isExisting) {
        if (ioctl(tapSock_, SIOCGIFHWADDR, &ifr_)) {
            LOGF("Failed to get MAC address.[{}]", getTapErrMsg());
            return false;
        }
        std::memcpy(mac_.addr, ifr_.ifr_hwaddr.sa_data, 6);
    } else {
        if (ioctl(fd_, TUNSETPERSIST, 1)) {
            LOGF("Failed to set persistent mode.[{}]", getTapErrMsg());
            return false;
        }

        mac_.generateMac();
        ifr_.ifr_hwaddr.sa_family = ARPHRD_ETHER;
        std::memcpy(ifr_.ifr_hwaddr.sa_data, mac_.addr, 6);
        if (ioctl(tapSock_, SIOCSIFHWADDR, &ifr_)) {
            LOGF("Failed to set MAC address.[{}]", getTapErrMsg());
            return false;
        }
    }

    ifr_.ifr_mtu = TAP_MTU_SIZE;
    if (ioctl(tapSock_, SIOCSIFMTU, &ifr_)) {
        LOGF("Failed to set MTU to [{}].[{}]", TAP_MTU_SIZE, getTapErrMsg());
        return false;
    }

    ifr_.ifr_qlen = TAP_QLEN;
    if (ioctl(tapSock_, SIOCSIFTXQLEN, &ifr_)) {
        LOGF("Failed to set qlen to [{}].[{}]", TAP_QLEN, getTapErrMsg());
        return false;
    }

    if (ioctl(tapSock_, SIOCGIFFLAGS, &ifr_)) {
        LOGF("Failed to get flags.[{}]", getTapErrMsg());
        return false;
    }
    ifr_.ifr_flags |= (IFF_UP | IFF_RUNNING);
    if (ioctl(tapSock_, SIOCSIFFLAGS, &ifr_)) {
        LOGF("Failed to set status to up.[{}]", getTapErrMsg());
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

bool TapDev::setIPv4Addr(const in_addr& ipv4Addr, uint8_t netIdLen)
{
    sockaddr_in* addr = (sockaddr_in*)&ifr_.ifr_addr;
    addr->sin_family = AF_INET;

    addr->sin_addr.s_addr = ipv4Addr.s_addr;
    if (ioctl(tapSock_, SIOCSIFADDR, &ifr_)) {
        LOGE("Failed to set IPv4 address to {}.[{}]", ipv4Addr, getTapErrMsg());
        return false;
    }

    addr->sin_addr.s_addr = (netIdLen == 0) ? 0 : htonl(0xFFFFFFFFU << (32 - netIdLen));
    if (ioctl(tapSock_, SIOCSIFNETMASK, &ifr_)) {
        LOGE("Failed to set netmask to {}.[{}]", netIdLen, getTapErrMsg());
        return false;
    }

    LOGI("{} IP address set to {}/{}", TAP_NAME, ipv4Addr, netIdLen);
    return true;
}
