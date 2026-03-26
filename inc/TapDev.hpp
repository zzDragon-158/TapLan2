#pragma once
#include    <iostream>
#include    <cstdint>
#include    <cstring>
#include    <sstream>
#include    <functional>
#include    "Common.hpp"

#ifdef      _WIN32
#include    <string>
#include    <stack>
#include    <vector>
#include    <mutex>
#include    <thread>
#include    <filesystem>

typedef     HANDLE              TapFd;
const size_t REG_BUF_SIZE = 256;
struct NetAdaptInfo {
    CHAR netCfgInstId[REG_BUF_SIZE];
    DWORD netInstIdLen;
    CHAR devInstId[REG_BUF_SIZE];
    DWORD devInstIdLen;
    CHAR name[REG_BUF_SIZE];
    DWORD nameLen;
    DWORD mediaStatus;
    DWORD mediaStatusLen;

    NetAdaptInfo(): netCfgInstId{},
                    netInstIdLen(REG_BUF_SIZE),
                    devInstId{},
                    devInstIdLen(REG_BUF_SIZE),
                    name{},
                    nameLen(REG_BUF_SIZE),
                    mediaStatus(TRUE),
                    mediaStatusLen(sizeof(mediaStatusLen)) {
        ;
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

class TapDev {
public:
    static TapDev*  ptr();
    TapFd           getFd() { return fd_; };
    bool            isFdVaild() { return fdValid_; };
    bool            close();
    void            getMacAddr(Mac& mac) { mac = mac_; };
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
    const char*     TAG = "[TapDev]";
    TapFd           fd_;
    bool            fdValid_;
    Mac             mac_;
    uint64_t        writeBytes_;
    uint64_t        writeErrs_;
    uint64_t        readBytes_;
    uint64_t        readErrs_;

    TapDev();
    ~TapDev();
    bool            open();
};

inline TapDev* TapDev::ptr()
{
    static TapDev ins;
    return &ins;
}
