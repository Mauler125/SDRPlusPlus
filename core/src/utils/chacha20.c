#include "chacha20.h"

/******************************************************************************
 *                                   SOURCE                                   *
 ******************************************************************************/

static inline uint32_t ChaCha20_Rotl32(uint32_t x, int n) 
{
	return (x << n) | (x >> (32 - n));
}

static inline uint32_t ChaCha20_Pack4(const uint8_t *a)
{
	uint32_t res = 0;
	res |= (uint32_t)a[0] << 0 * 8;
	res |= (uint32_t)a[1] << 1 * 8;
	res |= (uint32_t)a[2] << 2 * 8;
	res |= (uint32_t)a[3] << 3 * 8;
	return res;
}

static void ChaCha20_InitBlock(struct ChaCha20_Ctx_s*ctx, ChaCha20_Key256_t key, ChaCha20_Nonce96_t nonce)
{
	memcpy(ctx->key, key, sizeof(ctx->key));
	memcpy(ctx->nonce, nonce, sizeof(ctx->nonce));

	const uint8_t *magic_constant = (uint8_t*)"expand 32-byte k";
	ctx->state[0] = ChaCha20_Pack4(magic_constant + 0 * 4);
	ctx->state[1] = ChaCha20_Pack4(magic_constant + 1 * 4);
	ctx->state[2] = ChaCha20_Pack4(magic_constant + 2 * 4);
	ctx->state[3] = ChaCha20_Pack4(magic_constant + 3 * 4);
	ctx->state[4] = ChaCha20_Pack4(key + 0 * 4);
	ctx->state[5] = ChaCha20_Pack4(key + 1 * 4);
	ctx->state[6] = ChaCha20_Pack4(key + 2 * 4);
	ctx->state[7] = ChaCha20_Pack4(key + 3 * 4);
	ctx->state[8] = ChaCha20_Pack4(key + 4 * 4);
	ctx->state[9] = ChaCha20_Pack4(key + 5 * 4);
	ctx->state[10] = ChaCha20_Pack4(key + 6 * 4);
	ctx->state[11] = ChaCha20_Pack4(key + 7 * 4);

	// 64 bit counter initialized to zero by default.
	ctx->state[12] = 0;
	ctx->state[13] = ChaCha20_Pack4(nonce + 0 * 4);
	ctx->state[14] = ChaCha20_Pack4(nonce + 1 * 4);
	ctx->state[15] = ChaCha20_Pack4(nonce + 2 * 4);

	memcpy(ctx->nonce, nonce, sizeof(ctx->nonce));
}

static void ChaCha20_Block_SetCounter(struct ChaCha20_Ctx_s*ctx, uint64_t counter)
{
	ctx->state[12] = (uint32_t)counter;
	ctx->state[13] = ChaCha20_Pack4(ctx->nonce + 0 * 4) + (uint32_t)(counter >> 32);
}

static void ChaCha20_Block_Next(struct ChaCha20_Ctx_s*ctx) {
	// This is where the crazy voodoo magic happens.
	// Mix the bytes a lot and hope that nobody finds out how to undo it.
	for (int i = 0; i < 16; i++)
	{
		ctx->keyStream32[i] = ctx->state[i];
	}

#define CHACHA20_QUARTERROUND(x, a, b, c, d) \
	x[a] += x[b]; x[d] = ChaCha20_Rotl32(x[d] ^ x[a], 16); \
	x[c] += x[d]; x[b] = ChaCha20_Rotl32(x[b] ^ x[c], 12); \
	x[a] += x[b]; x[d] = ChaCha20_Rotl32(x[d] ^ x[a], 8); \
	x[c] += x[d]; x[b] = ChaCha20_Rotl32(x[b] ^ x[c], 7);

	for (int i = 0; i < 10; i++) 
	{
		CHACHA20_QUARTERROUND(ctx->keyStream32, 0, 4, 8, 12)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 1, 5, 9, 13)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 2, 6, 10, 14)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 3, 7, 11, 15)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 0, 5, 10, 15)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 1, 6, 11, 12)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 2, 7, 8, 13)
		CHACHA20_QUARTERROUND(ctx->keyStream32, 3, 4, 9, 14)
	}
#undef CHACHA20_QUARTERROUND

	for (int i = 0; i < 16; i++)
	{
		ctx->keyStream32[i] += ctx->state[i];
	}

	uint32_t *counter = ctx->state + 12;
	counter[0]++; // increment counter
	if (0 == counter[0])
	{
		// wrap around occurred, increment higher 32 bits of counter
		counter[1]++;
		// Limited to 2^64 blocks of 64 bytes each.
		// If you want to process more than 1180591620717411303424 bytes
		// you have other problems.
		// We could keep counting with counter[2] and counter[3] (nonce),
		// but then we risk reusing the nonce which is very bad.
		assert(0 != counter[1]);
	}
}

void ChaCha20_Init(struct ChaCha20_Ctx_s*ctx, ChaCha20_Key256_t key, ChaCha20_Nonce96_t nonce, uint64_t counter)
{
	memset(ctx, 0, sizeof(struct ChaCha20_Ctx_s));

	ChaCha20_InitBlock(ctx, key, nonce);
	ChaCha20_Block_SetCounter(ctx, counter);

	ctx->counter = counter;
	ctx->position = 64;
}

void ChaCha20_Xor(struct ChaCha20_Ctx_s* ctx, uint8_t* bytes, size_t n_bytes)
{
	const uint8_t* ks_base = (uint8_t*)ctx->keyStream32;
	const uint8_t* ks = ks_base + ctx->position;

	while (n_bytes > 0)
	{
		if (ctx->position == 64)
		{
			ChaCha20_Block_Next(ctx);
			ctx->position = 0;
		}

		const size_t remaining = 64 - ctx->position;
		const size_t todo = (n_bytes < remaining) ? n_bytes : remaining;
		size_t i = 0;

		for (; i + 8 <= todo; i += 8)
		{
			*(uint64_t*)(bytes + i) ^= *(const uint64_t*)(ks + i);
		}

		for (; i < todo; i++)
		{
			bytes[i] ^= ks[i];
		}

		bytes += todo;
		n_bytes -= todo;
		ctx->position += todo;
	}
}
