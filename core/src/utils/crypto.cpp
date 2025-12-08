#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "flog.h"
#include "chacha20.h"
#include "poly1305.h"

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <ntstatus.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#if defined(__APPLE__)
#include <Security/Security.h>
#endif
#endif
#include "crypto.h"

#define CRYPTRAND_KEY_INTERVAL (4 * 1024)

struct CryptRandState_s {
    ChaCha20_Ctx_s cipher;
    uint8_t currentKey[CHACHA20_KEY_LEN];
    int32_t bytesLeft;
    bool initialized;

#if defined(__linux__) || defined(__ANDROID__)
    int algorithmHandle;
#elif defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithmHandle;
#endif
};

static CryptRandState_s s_randomEngineState = { 0 };

static int32_t Plat_GetEntropy(uint8_t* buf, int32_t size) {
#if defined(__linux__) || defined(__ANDROID__)
    if (read(s_randomEngineState.algorithmHandle, buf, size) == size) {
        return 0;
    }
    flog::error("Plat_GetEntropy: read(/dev/urandom) failed");
#elif defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, size, buf) == 0) {
        return 0;
    }
    flog::error("Plat_GetEntropy: SecRandomCopyBytes() failed");
#elif defined(_WIN32)
    if (BCryptGenRandom(s_randomEngineState.algorithmHandle, buf, size, 0) == STATUS_SUCCESS) {
        return 0;
    }
    flog::error("Plat_GetEntropy: BCryptGenRandom() failed");
#endif
    return -1;
}

static int32_t Crypto_ReseedKey() {
    if (Plat_GetEntropy(s_randomEngineState.currentKey, sizeof(s_randomEngineState.currentKey)) != 0) {
        flog::error("Crypto_ReseedKey: unable to pull entropy for cipher key");
        return -1;
    }
    s_randomEngineState.bytesLeft = CRYPTRAND_KEY_INTERVAL;
    return 0;
}

int32_t Crypto_InitRandom() {
    if (s_randomEngineState.initialized) { return 0; }

    memset(&s_randomEngineState, 0, sizeof(s_randomEngineState));

#if defined(__linux__) || defined(__ANDROID__)
    if ((s_randomEngineState.algorithmHandle = open("/dev/urandom", O_RDONLY)) < 0) {
        flog::error("Crypto_InitRandom: failed to open /dev/urandom");
        return -1;
    }
#elif defined(_WIN32)
    if (BCryptOpenAlgorithmProvider(&s_randomEngineState.algorithmHandle, BCRYPT_RNG_ALGORITHM, NULL, 0) != STATUS_SUCCESS) {
        flog::error("Crypto_InitRandom: failed to open bcrypt algorithm provider");
        return -1;
    }
#endif

    if (Crypto_ReseedKey() < 0) {
        return -1;
    }

    s_randomEngineState.initialized = true;
    return 0;
}

void Crypto_ShutdownRandom() {
    if (!s_randomEngineState.initialized) { return; }

#if defined(__linux__) || defined(__ANDROID__)
    if (s_randomEngineState.algorithmHandle >= 0) close(s_randomEngineState.algorithmHandle);
#elif defined(_WIN32)
    if (s_randomEngineState.algorithmHandle) BCryptCloseAlgorithmProvider(s_randomEngineState.algorithmHandle, 0);
#endif

    memset(&s_randomEngineState, 0, sizeof(s_randomEngineState));
}

void Crypto_GenerateRandom(uint8_t* buf, int32_t size) {
    assert(s_randomEngineState.initialized);

    ChaCha20_Nonce96_t nonce;
    bool success = true;

    if (s_randomEngineState.bytesLeft == 0) {
        success &= (Crypto_ReseedKey() == 0);
    }

    success &= (Plat_GetEntropy(nonce, sizeof(nonce)) == 0);

    if (!success) {
        flog::error("Crypto_GenerateRandom: critical entropy failure; aborting...");
        std::abort(); // Can't guarantee security at this point so just abort
    }

    ChaCha20_Init(&s_randomEngineState.cipher, s_randomEngineState.currentKey, nonce, 0);

    memset(buf, 0, size);
    ChaCha20_Xor(&s_randomEngineState.cipher, buf, size);

    if (size >= s_randomEngineState.bytesLeft) {
        s_randomEngineState.bytesLeft = 0;
    }
    else {
        s_randomEngineState.bytesLeft -= size;
    }
}

static void Crypto_IncrementalXor(ChaCha20_Ctx_s* ctx,
                                  const ChaCha20_Key256_t key,
                                  const ChaCha20_Nonce96_t nonce,
                                  uint8_t* const input,
                                  const size_t len) {
    ChaCha20_Init(ctx, key, nonce, ctx->counter);
    ChaCha20_Xor(ctx, input, len);

    ctx->counter++;
}

static CryptoResult_e Crypto_DoCrypt(ChaCha20_Ctx_s* ctx,
                                     const ChaCha20_Key256_t key,
                                     const ChaCha20_Nonce96_t nonce,
                                     Poly1305_Tag_t tag,
                                     const uint8_t* ad,
                                     const size_t adLen,
                                     uint8_t* in,
                                     const size_t inLen,
                                     const bool encrypt) {
    Poly1305_Key_t polyKey;
    memset(polyKey, 0, sizeof(polyKey));

    // Generate poly key
    Crypto_IncrementalXor(ctx, key, nonce, polyKey, sizeof(polyKey));

    if (encrypt) {
        Crypto_IncrementalXor(ctx, key, nonce, in, inLen);

        if (tag) {
            Poly1305_GetTag(polyKey, ad, adLen, in, inLen, tag);
        }

        return CRYPTO_OK;
    }

    if (tag) {
        Poly1305_Tag_t polyTag;
        Poly1305_GetTag(polyKey, ad, adLen, in, inLen, polyTag);

        if (memcmp(tag, polyTag, POLY1305_TAG_LEN) != 0) {
            return CRYPTO_INVALID_MAC;
        }
    }

    Crypto_IncrementalXor(ctx, key, nonce, in, inLen);
    return CRYPTO_OK;
}

CryptoResult_e Crypto_Encrypt(ChaCha20_Ctx_s* ctx,
                              const ChaCha20_Key256_t key,
                              const ChaCha20_Nonce96_t nonce,
                              Poly1305_Tag_t tag,
                              const uint8_t* ad,
                              const size_t adLen,
                              uint8_t* in,
                              const size_t inLen) {
    return Crypto_DoCrypt(ctx, key, nonce, tag, ad, adLen, in, inLen, true);
}

CryptoResult_e Crypto_Decrypt(ChaCha20_Ctx_s* ctx,
                              const ChaCha20_Key256_t key,
                              const ChaCha20_Nonce96_t nonce,
                              Poly1305_Tag_t tag,
                              const uint8_t* ad,
                              const size_t adLen,
                              uint8_t* in,
                              const size_t inLen) {
    return Crypto_DoCrypt(ctx, key, nonce, tag, ad, adLen, in, inLen, false);
}
