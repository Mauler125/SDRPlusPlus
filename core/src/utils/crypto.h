#pragma once
#include <stdint.h>

int32_t Crypto_InitRandom();
void Crypto_ShutdownRandom();
void Crypto_GenerateRandom(uint8_t* buf, int32_t size);
