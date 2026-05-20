#pragma     once
#include    <cstdint>
#if defined(__linux__)
#include    <net/if.h>          // for struct ifreq, IFNAMSIZ
#endif
#include    "Common.hpp"

class TapDev {
public:
    static TapDev&  instance() {
        static TapDev ins;
        return ins;
    }
    TapFd           getFd() { return fd_; };
    bool            isFdValid() { return (fd_ != INVALID_TAPFD); };
    bool            close();
    const Mac&      getMac() { return mac_; };
    bool            setIPv4Addr(const in_addr& ipv4Addr, uint8_t netIdLen);

private:
    static const size_t             REG_BUF_SIZE = 256;
    static constexpr const char*    TAG = "[TapDev]";
    static constexpr const char*    TAP_NAME = "TapLan";
    static const size_t             TAP_MTU_SIZE = 1418;
    static const size_t             TAP_QLEN = 5000;

    TapFd           fd_= INVALID_TAPFD;
    Mac             mac_{};

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
    } tapInfo_{};

    bool            initTapInfo(HKEY adaptKey, LPCSTR adaptIdx);
    bool            findExistingTap();
    bool            createNewTap();
#elif       __linux__
    SockFd          tapSock_ = INVALID_SOCKFD;
    ifreq           ifr_{};
#endif
};

inline TapDev& g_tapDev = TapDev::instance();
