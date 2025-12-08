#pragma once
#include <stdint.h>
#include "chacha20.h"
#include "poly1305.h"

int32_t Crypto_InitRandom();
void Crypto_ShutdownRandom();
void Crypto_GenerateRandom(uint8_t* buf, int32_t size);

enum CryptoResult_e {
    CRYPTO_INVALID_MAC = -1,
    CRYPTO_OK
};

CryptoResult_e Crypto_Encrypt(ChaCha20_Ctx_s* ctx,
                              const ChaCha20_Key256_t key,
                              const ChaCha20_Nonce96_t nonce,
                              Poly1305_Tag_t tag,
                              const uint8_t* ad,
                              const size_t adLen,
                              uint8_t* in,
                              const size_t inLen);

CryptoResult_e Crypto_Decrypt(ChaCha20_Ctx_s* ctx,
                              const ChaCha20_Key256_t key,
                              const ChaCha20_Nonce96_t nonce,
                              Poly1305_Tag_t tag,
                              const uint8_t* ad,
                              const size_t adLen,
                              uint8_t* in,
                              const size_t inLen);
