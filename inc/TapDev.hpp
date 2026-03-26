#pragma     once
#include    <iostream>
#include    <cstdint>
#include    <cstring>
#include    <string>
#include    <sstream>
#include    <functional>
#include    "Common.hpp"

#ifdef      _WIN32
#include    <filesystem>

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

#else
#error      "unsupported platform!"

#endif

// FIXME: It is best not to use a singleton.
#define     TapDevPtr           TapDev::ptr()

class TapDev {
public:
    static TapDev*  ptr();
    TapFd           getFd() { return fd_; };
    bool            isFdValid() {  return (fd_ != INVALID_TAPFD); };
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
    static const size_t             REG_BUF_SIZE = 256;
    static constexpr const char*    TAG = "[TapDev]";
    static constexpr const char*    TAP_NAME = "TapLan";
    static const size_t             TAP_MTU_SIZE = 1418;
    static const size_t             TAP_QLEN = 5000;

    TapFd           fd_;
    Mac             mac_;
    uint64_t        writeBytes_;
    uint64_t        writeErrs_;
    uint64_t        readBytes_;
    uint64_t        readErrs_;

    TapDev();
    ~TapDev();
    bool            open();

#ifdef      _WIN32
    struct NetAdaptInfo {
        CHAR netCfgInstId[REG_BUF_SIZE];
        DWORD netInstIdLen;
        CHAR devInstId[REG_BUF_SIZE];
        DWORD devInstIdLen;
        CHAR name[REG_BUF_SIZE];
        DWORD nameLen;
        DWORD mediaStatus;
        DWORD mediaStatusLen;

        NetAdaptInfo() noexcept;
    } tapInfo_;

    bool            initTapInfo(HKEY adaptKey, LPCSTR adaptIdx);
    bool            findExistingTap();
    bool            createNewTap();

#elif       __linux__
    SockFd          tapSock_;
    ifreq           ifr_;

#endif
};

inline TapDev* TapDev::ptr()
{
    static TapDev ins;
    return &ins;
}
