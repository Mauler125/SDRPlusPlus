#pragma once

// See https://stackoverflow.com/a/52045523
// The buffer sizes mentioned by this answer is without '\0' terminator!
// This is also for the IEEE-754 specification (scientific notation).
#define FLT_STR_BUF_SIZE 17
#define DBL_STR_BUF_SIZE 25

// Sufficient for a signed 16 bit integer [ -128 to +127 ] + '\0'.
#define I8_STR_BUF_SIZE 5

// Sufficient for a signed 16 bit integer [ -32768 to +32767 ] + '\0'.
#define I16_STR_BUF_SIZE 7

// Sufficient for a signed 32 bit integer [ -2147483648 to +2147483647 ] + '\0'.
#define I32_STR_BUF_SIZE 12

// Sufficient for a signed 64 bit integer [ -9223372036854775807 to +9223372036854775807 ] + '\0'.
#define I64_STR_BUF_SIZE 21

// Sufficient for a 64 bit pointer [ ffffffffffffffff ] + '\0'.
#define PTR_STR_BUF_SIZE 17
