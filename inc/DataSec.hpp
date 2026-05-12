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
    template<typename T>
    bool encrypt(AeadPacket* packet, T& payloadLen);
    template<typename T>
    bool decrypt(AeadPacket* packet, T& payloadLen);
    template<typename T>
    static bool decryptWithoutCheck(AeadPacket* packet, T& payloadLen);
    bool isSameSession(const Nonce& nonce) {
        return (nonce.sessionId == recvNonce_.sessionId);
    };
    const Nonce& getSendNonce() { return sendNonce_; }
    void setRecvNonce(const Nonce& nonce) {
        recvNonce_ = nonce;
        recvMaxSeen_ = 0;
        recvBitmap_ = 0;
        LOGD(TAG, "set recv nonce: send[%X] recv[%X].", sendNonce_.sessionId, recvNonce_.sessionId);
    }
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
    static uint8_t key_[crypto_aead_chacha20poly1305_IETF_KEYBYTES];

    Nonce sendNonce_;
    Nonce recvNonce_;
    uint64_t recvMaxSeen_;
    uint64_t recvBitmap_;

    bool checkReplay(uint64_t seq);
};
using AeadSessSPtr = std::shared_ptr<AeadSession>;

template<typename T>
bool AeadSession::encrypt(AeadPacket* packet, T& payloadLen)
{
    if (!s_initialized_) {
        return false;
    }

    packet->nonce = sendNonce_;
    ++sendNonce_.counter();

    unsigned long long outLen;
    crypto_aead_chacha20poly1305_ietf_encrypt(
        packet->payload, &outLen,
        packet->payload, payloadLen,
        NULL, 0,
        NULL,
        (uint8_t*)&packet->nonce,
        key_
    );

    payloadLen = static_cast<int32_t>(outLen);
    return true;
}

template<typename T>
bool AeadSession::decryptWithoutCheck(AeadPacket* packet, T& payloadLen)
{
    if (!initAeadSession()) {
        return false;
    }

    unsigned long long outLen;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(
        packet->payload, &outLen,
        NULL,
        packet->payload, payloadLen,
        NULL, 0,
        (uint8_t*)&packet->nonce, key_
    ) != 0) {
        LOGW(TAG, "Failed to decrypt.");
        return false;
    }

    payloadLen = static_cast<int32_t>(outLen);
    return true;
}

template<typename T>
bool AeadSession::decrypt(AeadPacket* packet, T& payloadLen)
{
    if (!initAeadSession()) {
        return false;
    }

    decryptWithoutCheck(packet, payloadLen);

    if (!checkReplay(packet->nonce.counter())) {
        LOGW(TAG, "Duplicate package received!");
        return false;
    }

    return true;
}
