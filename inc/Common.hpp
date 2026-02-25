#pragma     once
#include    <cstdint>
#include    <vector>
#include    <stack>
#include    <mutex>

#ifdef      _WIN32
#include    <winsock2.h>
#include    <ws2tcpip.h>
#include    <windows.h>

struct IOContext {
    OVERLAPPED overlapped;
    char buf[2048];
    DWORD bufLen;
    bool isPending;
    char token;

    void reset() {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        bufLen = 0;
        isPending = false;
    }
};

class IOPool {
private:
    std::vector<IOContext*> allContexts;
    std::stack<IOContext*> freeStack;
    std::mutex mtx;

public:
    IOPool(size_t poolSize) {
        for (size_t i = 0; i < poolSize; ++i) {
            IOContext* ctx = new IOContext();
            ctx->reset();
            allContexts.push_back(ctx);
            freeStack.push(ctx);
        }
    }

    IOContext* acquire() {
        std::lock_guard<std::mutex> lock(mtx);
        if (freeStack.empty()) return nullptr;
        IOContext* ctx = freeStack.top();
        freeStack.pop();
        ctx->isPending = true;
        return ctx;
    }

    void release(IOContext* ctx) {
        ctx->reset();
        std::lock_guard<std::mutex> lock(mtx);
        freeStack.push(ctx);
    }

    ~IOPool() {
        for (auto ctx : allContexts) delete ctx;
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
