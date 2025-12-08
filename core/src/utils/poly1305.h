#ifndef POLY1305_AUTH_H
#define POLY1305_AUTH_H

/******************************************************************************
 *                                   HEADER                                   *
 ******************************************************************************/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define POLY1305_KEY_LEN    32
#define POLY1305_TAG_LEN    16
#define POLY1305_BLOCK_SIZE 16

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Poly1305_Key_t[POLY1305_KEY_LEN];
typedef uint8_t Poly1305_Tag_t[POLY1305_TAG_LEN];

struct Poly1305_Ctx_s
{
	uint32_t r[5];
	uint32_t h[5];
	uint32_t pad[4];
	size_t leftover;
	uint8_t buffer[POLY1305_BLOCK_SIZE];
	uint8_t final;
};

void Poly1305_Init(struct Poly1305_Ctx_s* ctx, const Poly1305_Key_t key);
void Poly1305_Update(struct Poly1305_Ctx_s* ctx, const uint8_t* m, size_t bytes);
void Poly1305_Finish(struct Poly1305_Ctx_s* ctx, Poly1305_Tag_t mac);
void Poly1305_Auth(Poly1305_Tag_t mac, const uint8_t* m, size_t bytes, const Poly1305_Key_t key);
void Poly1305_GetTag(Poly1305_Key_t poly_key, const void* ad, size_t ad_len, const void* ct, size_t ct_len, Poly1305_Tag_t tag);

#ifdef __cplusplus
}
#endif

#endif // POLY1305_AUTH_H
