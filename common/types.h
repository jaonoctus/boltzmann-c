#ifndef BOLTZMANN_COMMON_TYPES_H
#define BOLTZMANN_COMMON_TYPES_H
/* Short integer type names, in the spirit of ccan/short_types as used by
 * Core Lightning.  Everything in this code base sizes its integers
 * explicitly: aggregate bitmasks are u32, satoshi amounts and combination
 * counts are 64-bit.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif /* BOLTZMANN_COMMON_TYPES_H */
