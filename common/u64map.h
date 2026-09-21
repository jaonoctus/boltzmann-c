#ifndef BOLTZMANN_COMMON_U64MAP_H
#define BOLTZMANN_COMMON_U64MAP_H
/*
 * u64map: a minimal open-addressing hash map from u64 keys to u64 values.
 *
 * The linker uses it where the Python code uses a dict keyed by a pair of
 * aggregates: the two 32-bit bitmasks are packed into one 64-bit key.
 * There is no deletion; maps are built up and then read back in full.
 */
#include "common/tal.h"
#include "common/types.h"

struct u64map_entry {
	bool used;
	u64 key;
	u64 value;
};

struct u64map {
	struct u64map_entry *slots;	/* tal_arr, always a power of two */
	size_t count;			/* used entries */
};

/**
 * u64map_new - allocate an empty map under @ctx.
 */
struct u64map *u64map_new(const tal_t *ctx);

/**
 * u64map_add - add @delta to the value stored for @key (starting at 0).
 */
void u64map_add(struct u64map *map, u64 key, u64 delta);

/**
 * u64map_first / u64map_next - iterate over used entries in slot order.
 */
struct u64map_entry *u64map_first(const struct u64map *map);
struct u64map_entry *u64map_next(const struct u64map *map,
				 const struct u64map_entry *e);

#define u64map_foreach(map, e) \
	for ((e) = u64map_first(map); (e); (e) = u64map_next((map), (e)))

/**
 * pack_pair - combine two 32-bit masks into one 64-bit map key.
 */
static inline u64 pack_pair(u32 hi, u32 lo)
{
	return ((u64)hi << 32) | lo;
}

static inline u32 pair_hi(u64 key)
{
	return (u32)(key >> 32);
}

static inline u32 pair_lo(u64 key)
{
	return (u32)key;
}

#endif /* BOLTZMANN_COMMON_U64MAP_H */
