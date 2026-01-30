#pragma once
#include    <iostream>
#include    <cstdint>
#include    <sstream>

#ifdef      _WIN32
// #include    <WS2tcpip.h>
#include    "WinHeaders.hpp"

#elif       __linux__
#include    <unistd.h>                              // for close
#include    <fcntl.h>                               // for open, O_RDWR
#include    <cstring>                               // for memset, strncpy
#include    <sys/ioctl.h>                           // for ioctl, TUNSETIFF
#include    <net/if.h>                              // for struct ifreq, IFNAMSIZ
#include    <linux/if_tun.h>                        // for IFF_TAP, IFF_NO_PI;
#include    <arpa/inet.h>                           // for in_addr
#include    <poll.h>                                // for poll, pollfd

#else
#error      "unsupported platform!"

#endif

#define     TAP_NAME                                "TapLan"
#define     ETHERNET_HEADER_LEN                     14
#define     ETHERTYPE_ARP                           0x0806
#define     ETHERNET_MAC_LEN                        6
#define     TapDevPtr                               TapDev::ptr()

#pragma pack(push, 1)
struct EtherHeader {
    uint8_t dst[ETHERNET_MAC_LEN];
    uint8_t src[ETHERNET_MAC_LEN];
    uint16_t type;
};
#pragma pack(pop)
extern int tap_fd;

class TapDev {
public:
    bool            open();
    bool            close();
    ssize_t         write(const void* buf, size_t bufLen);
    ssize_t         read(void* buf, size_t bufLen, int timeout = -1);
    bool            getMacAddr(uint8_t* buf, size_t bufLen);
    bool            setIpv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen);
    static TapDev*  ptr();
private:
    uint8_t     macAddress_[ETHERNET_MAC_LEN];
    uint64_t    writeErrCnt_;
    uint64_t    readErrCnt_;

    TapDev();
    ~TapDev();
};

inline TapDev* TapDev::ptr()
{
    static TapDev ins;
    return &ins;
}
