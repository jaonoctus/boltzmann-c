#include "common/u64map.h"

#include <assert.h>

#define INITIAL_SLOTS 64

struct u64map *u64map_new(const tal_t *ctx)
{
	struct u64map *map = tal(ctx, struct u64map);

	map->slots = tal_arrz(map, struct u64map_entry, INITIAL_SLOTS);
	map->count = 0;
	return map;
}

/* splitmix64 finaliser: cheap and well distributed. */
static size_t hash_key(u64 key, size_t nslots)
{
	key ^= key >> 30;
	key *= 0xbf58476d1ce4e5b9ULL;
	key ^= key >> 27;
	key *= 0x94d049bb133111ebULL;
	key ^= key >> 31;
	return (size_t)key & (nslots - 1);
}

static struct u64map_entry *find_slot(struct u64map_entry *slots, u64 key)
{
	size_t nslots = tal_count(slots);
	size_t i = hash_key(key, nslots);

	/* Linear probing: the table is kept at most half full. */
	while (slots[i].used && slots[i].key != key)
		i = (i + 1) & (nslots - 1);
	return &slots[i];
}

static void grow(struct u64map *map)
{
	struct u64map_entry *old = map->slots;
	size_t i;

	map->slots = tal_arrz(map, struct u64map_entry, tal_count(old) * 2);
	for (i = 0; i < tal_count(old); i++) {
		if (old[i].used)
			*find_slot(map->slots, old[i].key) = old[i];
	}
	tal_free(old);
}

void u64map_add(struct u64map *map, u64 key, u64 delta)
{
	struct u64map_entry *e;

	if (map->count * 2 >= tal_count(map->slots))
		grow(map);
	e = find_slot(map->slots, key);
	if (!e->used) {
		e->used = true;
		e->key = key;
		e->value = 0;
		map->count++;
	}
	e->value += delta;
}

/* Returns the first used slot at or after @e, or NULL. */
static struct u64map_entry *scan_from(const struct u64map *map,
				      const struct u64map_entry *e)
{
	const struct u64map_entry *end = map->slots + tal_count(map->slots);

	for (; e < end; e++) {
		if (e->used)
			return (struct u64map_entry *)e;
	}
	return NULL;
}

struct u64map_entry *u64map_first(const struct u64map *map)
{
	return scan_from(map, map->slots);
}

struct u64map_entry *u64map_next(const struct u64map *map,
				 const struct u64map_entry *e)
{
	return scan_from(map, e + 1);
}
