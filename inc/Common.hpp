#pragma     once
#include    <cstdint>
#include    <vector>
#include    <stack>
#include    <mutex>
#include    <LogMgr.hpp>

#ifdef      _WIN32
#include    <winsock2.h>
#include    <ws2tcpip.h>
#include    <windows.h>

using SockFd = SOCKET;
constexpr SockFd INVALID_SOCKFD = INVALID_SOCKET;
using TapFd = HANDLE;
inline const TapFd INVALID_TAPFD = INVALID_HANDLE_VALUE;

static std::string getErrMsg(DWORD errorCode)
{
    if (errorCode == 0)
        return "Success";

    LPSTR msgBuf = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        // MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),   // system language
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),   // english
        (LPSTR)&msgBuf,
        0,
        NULL
    );
    std::string msg(msgBuf, size);
    LocalFree(msgBuf);

    if (!msg.empty() && msg.back() == '\n') msg.pop_back();
    if (!msg.empty() && msg.back() == '\r') msg.pop_back();

    return msg;
}

static std::string getSockErr()
{
    return getErrMsg(WSAGetLastError());
}

static std::string getIoErr()
{
    return getErrMsg(GetLastError());
}

#elif       __linux__
#include    <cstring>           // for strerror
#include    <arpa/inet.h>
#include    <sys/socket.h>
#include    <sys/uio.h>
#include    <liburing.h>

using SockFd = int;
constexpr SockFd INVALID_SOCKFD = -1;
using TapFd = int;
constexpr TapFd INVALID_TAPFD = -1;

static std::string getErrMsg(int errCode)
{
    if (errCode == 0)
        return "Success";

    return strerror(errCode);
}

static std::string getSockErr()
{
    return getErrMsg(errno);
}

static std::string getIoErr()
{
    return getErrMsg(errno);
}

#endif

struct Mac {
    uint8_t addr[6];

    operator uint64_t() {
        uint64_t num = 0;
        __builtin_memcpy(&num, addr, 6);
        return num;
    };
    Mac& operator =(const Mac& m) {
        __builtin_memcpy(&this->addr, &m.addr, 6);
        return *this;
    }
    Mac& operator =(const uint64_t& m) {
        __builtin_memcpy(&this->addr, &m, 6);
        return *this;
    }
    uint8_t& operator [](uint8_t i) {
        return addr[i];
    }
    void generateMac() {
        addr[0] = 0x02;
        addr[1] = 0x34;
        addr[2] = 0x60;

        auto now = std::chrono::high_resolution_clock::now();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        uint32_t seed = static_cast<uint32_t>(micros ^ (getpid() << 16));
        addr[3] = (seed >> 16) & 0xFF;
        addr[4] = (seed >> 8) & 0xFF;
        addr[5] = seed & 0xFF;
    }
    std::string getMacStr() {
        char buf[18];
    
        std::snprintf(
            buf,
            sizeof(buf),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]
        );
        
        return std::string(buf);
    }
};

struct EthHdr {
    Mac dst;
    Mac src;
    uint16_t type;
};

enum {
    TOKEN_UDP_RECV_MULTISHOT = 0,
    TOKEN_UDP_RECV,
    TOKEN_UDP_SEND,
    TOKEN_TAP_READ,
    TOKEN_TAP_WRITE,
};

enum RunModeT {
    RunMode_None = 0,
    RunMode_Server,
    RunMode_Client,
};

struct ConfigDataT {
    uint8_t     runMode;
    uint16_t    localPort;
    uint32_t    netNum;
    uint8_t     netNumLen;
    in6_addr    remoteAddr;
    uint16_t    remotePort;
    uint16_t    swPortIntvl;
    bool        isAioEnable;
    bool        noSync;
    bool        isRunning;
    Mac         mac;

    ConfigDataT(): runMode(RunMode_Server), localPort(3460),
                    netNum((192 << 24) + (168 << 16) + (208 << 8)),
                    netNumLen(24), remoteAddr{}, remotePort(0),
                    swPortIntvl(0), isAioEnable(false),
                    noSync(false), isRunning(false),
                    mac{} {
        // nothing to do
    }
};

const int IO_WAIT_TIME = 3;
const size_t DATA_BUF_SIZE = 2048;
extern ConfigDataT g_cfgData;
