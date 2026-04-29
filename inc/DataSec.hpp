#pragma     once
#include    "sodium.h"
#include    "Common.hpp"

union Nonce {
    struct {
        uint32_t sessionId;
        uint32_t counterLow;
        uint32_t counterHigh;
    };
    uint8_t data[crypto_aead_aes256gcm_NPUBBYTES];
};
static constexpr size_t NONCE_SIZE = sizeof(Nonce);

struct AeadPacket {
    Nonce nonce;
    uint8_t payload[];
};

class AeadSession {
public:
    AeadSession(const Mac& mac, const Nonce& nonce = { 0 });
    static bool initAeadSession();
    bool encrypt(AeadPacket* packet, int& payloadLen);
    bool decrypt(AeadPacket* packet, int& payloadLen);

private:
    static constexpr char TAG[] = "[DataSec]";
    static bool s_initialized_;
    static uint8_t key_[crypto_aead_aes256gcm_KEYBYTES];
    Nonce sendNonce_;

    Nonce recvNonce_;
    uint64_t recvMax_;
    uint64_t recvBitmap_;
};