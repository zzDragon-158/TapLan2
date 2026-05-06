#include    <ctime>
#include    "DataSec.hpp"
#include    "Common.hpp"
#include    "Config.hpp"

bool AeadSession::s_initialized_ = false;
uint8_t AeadSession::key_[crypto_aead_chacha20poly1305_IETF_KEYBYTES] = {};

AeadSession::AeadSession(const Mac& mac, const Nonce& nonce)
: sendNonce_{}
, recvNonce_{}
, recvMaxSeen_(0)
, recvBitmap_(0)
{
    static_assert(NONCE_SIZE == crypto_aead_chacha20poly1305_IETF_NPUBBYTES);
    if (!s_initialized_) {
        initAeadSession();
    }

    if (s_initialized_) {
        randombytes_buf(&sendNonce_.counter(), sizeof(sendNonce_.counter()));
        sendNonce_.counterHigh &= (1 << 24) - 1;
        sendNonce_.counterLow &= (1 << 24) - 1;

        sendNonce_.macEUI[0] = mac.addr[3];
        sendNonce_.macEUI[1] = mac.addr[4];
        sendNonce_.macEUI[2] = mac.addr[5];

        std::time_t now = std::time(nullptr);
        sendNonce_.macEUI[3] = now & 0xff;
    }

    recvNonce_ = nonce;
    LOGD(TAG, "new session: %u", recvNonce_.sessionId);
}

AeadSession::~AeadSession()
{
    LOGD(TAG, "del session: %u", recvNonce_.sessionId);
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

    // if (crypto_aead_aes256gcm_is_available() == 0) {
    //     LOGE(TAG, "Hardware does not support AES-GCM!");
    //     return false;
    // }

    const std::string& passwd = g_cfgData.passwd();
    LOGD(TAG, "passwd: %s", passwd.c_str());
    crypto_hash_sha256(key_, (const unsigned char*)passwd.c_str(), passwd.length());

    s_initialized_ = true;
    return true;
}

bool AeadSession::checkReplay(uint64_t seq)
{
    if (seq > recvMaxSeen_) {
        uint64_t shift = seq - recvMaxSeen_;

        if (shift >= 64) {
            recvBitmap_ = 1;
        } else {
            recvBitmap_ <<= shift;
            recvBitmap_ |= 1;
        }

        recvMaxSeen_ = seq;
        return true;
    }

    uint64_t delta = recvMaxSeen_ - seq;
    if (delta >= 64) {
        LOGW(TAG, "[%u]Seq of recv packet out of range.", recvNonce_.sessionId);
        return false;
    }

    uint64_t seqBit = 1ULL << delta;
    if (recvBitmap_ & seqBit) {
        LOGW(TAG, "[%u]Replay package detected.", recvNonce_.sessionId);
        return false;
    }

    recvBitmap_ |= seqBit;
    return true;
}
