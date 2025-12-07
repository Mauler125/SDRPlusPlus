#ifndef CHACHA20_CIPHER_H
#define CHACHA20_CIPHER_H

/******************************************************************************
 *                                   HEADER                                   *
 ******************************************************************************/

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CHACHA20_KEY_LEN 32
#define CHACHA20_IV_LEN  12

#ifdef __cplusplus 
extern "C" {
#endif

typedef uint8_t ChaCha20_Key256_t[CHACHA20_KEY_LEN];
typedef uint8_t ChaCha20_Nonce96_t[CHACHA20_IV_LEN];

struct ChaCha20_Ctx_s
{
	uint32_t keyStream32[16];
	size_t position;

	ChaCha20_Key256_t key;
	ChaCha20_Nonce96_t nonce;
	uint64_t counter;

	uint32_t state[16];
};

void ChaCha20_Init(struct ChaCha20_Ctx_s*ctx, ChaCha20_Key256_t key, ChaCha20_Nonce96_t nounc, uint64_t counter);
void ChaCha20_Xor(struct ChaCha20_Ctx_s*ctx, uint8_t *bytes, size_t n_bytes);

#ifdef __cplusplus 
}
#endif 

#endif // CHACHA20_CIPHER_H
