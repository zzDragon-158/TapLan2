#include    "DataSec.hpp"
#include    "Common.hpp"
#include    "Config.hpp"

bool AeadSession::s_initialized_ = false;
uint8_t AeadSession::key_[crypto_aead_aes256gcm_KEYBYTES] = {};

AeadSession::AeadSession(const Mac& mac, const Nonce& nonce)
{
    static_assert(NONCE_SIZE == crypto_aead_aes256gcm_NPUBBYTES);
    if (!s_initialized_) {
        initAeadSession();
    }

    if (s_initialized_) {
        randombytes_buf(&sendNonce_, sizeof(sendNonce_));
        sendNonce_.data[0] = mac.addr[3];
        sendNonce_.data[1] = mac.addr[4];
        sendNonce_.data[2] = mac.addr[5];
    }

    recvNonce_ = nonce;
}

bool AeadSession::initAeadSession()
{
    if (s_initialized_) {
        return true;
    }

    if (sodium_init() < 0) {
        LOGE(TAG, "Failed to init sodium.");
        return false;
    }

    if (crypto_aead_aes256gcm_is_available() == 0) {
        LOGE(TAG, "Hardware does not support AES-GCM!");
        return false;
    }

    const std::string& passwd = g_cfgData.passwd();
    crypto_hash_sha256(key_, (const unsigned char*)passwd.c_str(), passwd.length());

    s_initialized_ = true;
    return true;
}

bool AeadSession::encrypt(AeadPacket* packet, int& payloadLen)
{
    if (!s_initialized_) {
        return false;
    }

    packet->nonce = sendNonce_;
    unsigned long long outLen;
    crypto_aead_aes256gcm_encrypt(
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

bool AeadSession::decrypt(AeadPacket* packet, int& payloadLen)
{
    if (!s_initialized_) {
        return false;
    }

    unsigned long long outLen;
    if (crypto_aead_aes256gcm_decrypt(
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
