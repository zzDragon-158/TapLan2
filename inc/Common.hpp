#pragma     once
#include    <cstdint>

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
