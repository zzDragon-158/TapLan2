#pragma     once
#include    <cstdint>
#include    <string>
#include    <format>
#ifdef      _WIN32
#include    <winsock2.h>
#include    <ws2tcpip.h>
#include    <windows.h>

using PollFunc = int(*)(pollfd*, ULONG, INT);
constexpr PollFunc UnivPoll = WSAPoll;
using UnivPollFd = pollfd;
using SockFd = SOCKET;
constexpr SockFd INVALID_SOCKFD = INVALID_SOCKET;
using TapFd = HANDLE;
inline const TapFd INVALID_TAPFD = INVALID_HANDLE_VALUE;

std::string getErrMsg(DWORD errorCode);
std::string getSockErr();
std::string getIoErr();
#elif       __linux__
#include    <netinet/in.h>      // for in_addr, in6_addr
#include    <arpa/inet.h>       // for inet_ntop, inet_pton
#include    <poll.h>            // for poll

using PollFunc = int(*)(pollfd*, nfds_t, int);
constexpr PollFunc UnivPoll = poll;
using UnivPollFd = pollfd;
using SockFd = int;
constexpr SockFd INVALID_SOCKFD = -1;
using TapFd = int;
constexpr TapFd INVALID_TAPFD = -1;

std::string getErrMsg(int errCode);
std::string getSockErr();
std::string getIoErr();
#endif

constexpr int IO_WAIT_TIME = 3;

void delayExit(int code, int64_t delaySeconds = 0);

struct Mac {
    uint8_t addr[6];

    operator uint64_t() const {
        uint64_t num = 0;
        __builtin_memcpy(&num, addr, 6);
        return num;
    };
    Mac& operator =(const uint64_t& m) {
        __builtin_memcpy(&this->addr, &m, 6);
        return *this;
    }
    uint8_t& operator [](uint8_t i) {
        return addr[i];
    }
    void generateMac();
    std::string getMacStr() const;
};

template<>
struct std::formatter<Mac>: std::formatter<std::string_view> {
    auto format(const Mac& mac, std::format_context& ctx) const {
        char buf[32];

        auto pos = std::format_to_n(
            buf,
            sizeof(buf),
            "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
            mac.addr[0], mac.addr[1], mac.addr[2],
            mac.addr[3], mac.addr[4], mac.addr[5]
        );

        return std::formatter<std::string_view>::format(std::string_view(buf, 17), ctx);
    }
};

template <>
struct std::formatter<in6_addr>: std::formatter<std::string_view> {
    auto format(const in6_addr& addr, std::format_context& ctx) const {
        char buf[INET6_ADDRSTRLEN];

        const char* result = inet_ntop(AF_INET6, &addr, buf, sizeof(buf));
        if (result == nullptr) {
            return std::formatter<std::string_view>::format("Invalid_IPv6", ctx);
        }

        return std::formatter<std::string_view>::format(std::string_view(buf), ctx);
    }
};

template <>
struct std::formatter<in_addr>: std::formatter<std::string_view> {
    auto format(const in_addr& addr, std::format_context& ctx) const {
        char buf[INET_ADDRSTRLEN];

        const char* result = inet_ntop(AF_INET, &addr, buf, sizeof(buf));
        if (result == nullptr) {
            return std::formatter<std::string_view>::format("Invalid_IPv4", ctx);
        }

        return std::formatter<std::string_view>::format(std::string_view(buf), ctx);
    }
};
