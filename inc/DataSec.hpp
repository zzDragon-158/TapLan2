#pragma     once
#include    <memory>
#include    "sodium.h"
#include    "Common.hpp"

struct Nonce{
    uint32_t counterLow;
    uint32_t counterHigh;
    union {
        uint32_t sessionId;
        uint8_t macEUI[4];
    };

    uint64_t& counter() {
        uint64_t& couter = reinterpret_cast<uint64_t&>(counterLow);
        return couter;
    }
};
static constexpr size_t NONCE_SIZE = sizeof(Nonce);

struct AeadPacket {
    Nonce nonce;
    uint8_t payload[];
};

class AeadSession {
public:
    AeadSession(const Mac& mac, const Nonce& nonce = { 0 });
    ~AeadSession();
    static bool initAeadSession();
    bool encrypt(AeadPacket* packet, int& payloadLen);
    bool decrypt(AeadPacket* packet, int& payloadLen);
    bool isSameSession(const Nonce& nonce) {
        return (nonce.sessionId == recvNonce_.sessionId);
    };
    static Mac fetchMacFromNonce(const Nonce& nonce) {
        return Mac{
            0x02, 0x34, 0x60,
            nonce.macEUI[0],
            nonce.macEUI[1],
            nonce.macEUI[2]
        };
    }

private:
    static constexpr char TAG[] = "[DataSec]";
    static bool s_initialized_;
    static uint8_t key_[crypto_aead_aes256gcm_KEYBYTES];

    Nonce sendNonce_;
    Nonce recvNonce_;
    uint64_t recvMaxSeen_;
    uint64_t recvBitmap_;

    bool checkReplay(uint64_t seq);
};
using AeadSessSPtr = std::shared_ptr<AeadSession>;
