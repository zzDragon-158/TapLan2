#pragma     once
#include    <cstdint>
#include    <vector>
#include    <stack>
#include    <mutex>
#include    <LogMgr.hpp>

const size_t DATA_BUF_SIZE = 4096;
const size_t DATA_BUF_NUM = 256;

#ifdef      _WIN32
#include    <winsock2.h>
#include    <ws2tcpip.h>
#include    <windows.h>

class IOPool;
struct IOContext {
    IOPool* owner;
    OVERLAPPED overlapped;
    sockaddr_in6 addr;
    INT addrLen;
    uint8_t* buf;
    DWORD bufLen;
    uint8_t token;

    IOContext(): owner(nullptr), overlapped{},
                 addr{}, addrLen(sizeof(sockaddr_in6)),
                 buf(nullptr), bufLen(0), token(0) {
        buf = new uint8_t [2048] ;
    }

    ~IOContext() {
        delete [] buf;
    }

    void reset() {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};

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

#elif       __linux__
#include    <arpa/inet.h>
#include    <sys/socket.h>
#include    <sys/uio.h>
#include    <liburing.h>

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
};

enum {
    TOKEN_UDP_RECV  = 1,
    TOKEN_TAP_READ  = 2,
    TOKEN_TAP_WRITE = 3,
    TOKEN_UDP_SEND  = 4,
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
    uint16_t    switchPortInterval;
    bool        isAioEnable;
    bool        noSync;
    bool        isRunning;
    Mac         mac;

    ConfigDataT(): runMode(RunMode_Server), localPort(3460),
                    netNum((192 << 24) + (168 << 16) + (208 << 8)),
                    netNumLen(24), remoteAddr{}, remotePort(0),
                    switchPortInterval(0), isAioEnable(false),
                    noSync(false), isRunning(false),
                    mac{} {
        // nothing to do
    }
};

const int IO_WAIT_TIME = 3;
extern ConfigDataT g_cfgData;
