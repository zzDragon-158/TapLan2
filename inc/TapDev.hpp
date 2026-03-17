#pragma once
#include    <iostream>
#include    <cstdint>
#include    <cstring>
#include    <sstream>
#include    <functional>
#include    "Common.hpp"

#ifdef      _WIN32
// #include    <WS2tcpip.h>
#include    <string>
#include    <stack>
#include    <vector>
#include    <mutex>
#include    <thread>
#include    <filesystem>

#define     BUFFER_SIZE         1024

typedef     HANDLE              TapFd;
struct WinAdapterInfo {
    CHAR netCfgInstId[BUFFER_SIZE];
    DWORD netInstIdLen;
    CHAR devInstId[BUFFER_SIZE];
    DWORD devInstIdLen;
    BYTE name[BUFFER_SIZE];
    DWORD nameLen;
    DWORD mediaStatus;
    DWORD mediaStatusLen;

    WinAdapterInfo():   netCfgInstId{}, netInstIdLen(BUFFER_SIZE),
                        devInstId{}, devInstIdLen(BUFFER_SIZE),
                        name{}, nameLen(BUFFER_SIZE),
                        mediaStatus(TRUE), mediaStatusLen(sizeof(mediaStatusLen)) {
        // nothing to do
    }
};

#elif       __linux__
#include    <unistd.h>          // for close
#include    <fcntl.h>           // for open, O_RDWR
#include    <cstring>           // for memset, strncpy
#include    <sys/ioctl.h>       // for ioctl, TUNSETIFF
#include    <net/if.h>          // for struct ifreq, IFNAMSIZ
#include    <net/if_arp.h>      // for ARPHRD_ETHER
#include    <linux/if_tun.h>    // for IFF_TAP, IFF_NO_PI;
#include    <arpa/inet.h>       // for in_addr
#include    <poll.h>            // for poll, pollfd
#include    <errno.h>           // for errno

typedef     int                 TapFd;

#else
#error      "unsupported platform!"

#endif

#define     TAP_NAME            "TapLan"
#define     TAP_MTU_SIZE        1418
#define     TAP_QLEN            5000
#define     ETH_HDR_LEN         14
#define     ETH_MAC_LEN         6
#define     TapDevPtr           TapDev::ptr()

#pragma pack(push, 1)
struct EthHdr {
    uint8_t dst[ETH_MAC_LEN];
    uint8_t src[ETH_MAC_LEN];
    uint16_t type;
};
#pragma pack(pop)
extern TapFd tapFd;

class TapDev {
public:
    static TapDev*  ptr();
    bool            isFdVaild() { return fdValid_; };
    bool            close();
    ssize_t         write(const void* buf, size_t bufLen);
    ssize_t         read(void* buf, size_t bufLen, int timeout = -1);
    void            getMacAddr(Mac& mac);
    bool            setIPv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen);
    uint64_t        getWriteBytes() { return writeBytes_; };
    uint64_t        getWriteErrs() { return writeErrs_; };
    uint64_t        getReadBytes() { return readBytes_; };
    uint64_t        getReadErrs() { return readErrs_; };
    void            incWriteBytes(uint64_t v) { writeBytes_ += v; };
    void            incWriteErrs(uint64_t v) { writeErrs_ += v; };
    void            incReadBytes(uint64_t v) { readBytes_ += v; };
    void            incReadErrs(uint64_t v) { readErrs_ += v; };

private:
    bool            fdValid_;
    Mac             mac_;
    uint64_t        writeBytes_;
    uint64_t        writeErrs_;
    uint64_t        readBytes_;
    uint64_t        readErrs_;

    TapDev();
    ~TapDev();
    void            generateMac();
    bool            open();
};

inline TapDev* TapDev::ptr()
{
    static TapDev ins;
    return &ins;
}
