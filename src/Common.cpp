#include    "Common.hpp"
#include    "LogMgr.hpp"

#if         defined(__WIN32)
std::string getErrMsg(ErrorT err)
{
    if (err == 0)
        return "Success";

    LPSTR msgBuf = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
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
#elif       defined(__linux__)
#include    <cstring>           // for strerror
#include    <unistd.h>          // for getpid

std::string getErrMsg(ErrorT err)
{
    if (err == 0)
        return "Success";

    return strerror(err);
}
#endif

void Mac::generateMac() {
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

std::string Mac::getMacStr() const {
    char buf[18];

    std::snprintf(
        buf,
        sizeof(buf),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]
    );
    
    return std::string(buf);
}

void delayExit(int code, int64_t delaySeconds)
{
    if (delaySeconds > 0) {
        LOGR("Program will completely exit after {} seconds.\n", delaySeconds);
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
    }

    g_logMgr.terminate();
    exit(code);
}
