#ifndef BOLTZMANN_COMMON_TAL_H
#define BOLTZMANN_COMMON_TAL_H
/*
 * tal: hierarchical memory allocation (a small subset of ccan/tal).
 *
 * Every allocation has a parent ("context").  Freeing a parent frees all
 * of its descendants, so a function that builds a large temporary data
 * structure can hang everything off one context and release it with a
 * single tal_free().  This is the allocation style used throughout Core
 * Lightning and it removes almost all of the manual bookkeeping a C
 * reader normally has to track.
 *
 * Conventions used in this code base:
 *   - The first parameter of any function that allocates its result is
 *     `const tal_t *ctx`: the parent the result is attached to.
 *   - Passing a NULL context creates a root allocation the caller owns.
 *   - Arrays remember their element count: tal_count(arr).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef void tal_t;

/**
 * tal - allocate one object of the given type.
 * talz - same, zeroed.
 * tal_arr - allocate an array of `count` elements of the given type.
 * tal_arrz - same, zeroed.
 */
#define tal(ctx, type) \
	((type *)tal_alloc_((ctx), sizeof(type), 1, false, #type))
#define talz(ctx, type) \
	((type *)tal_alloc_((ctx), sizeof(type), 1, true, #type))
#define tal_arr(ctx, type, count) \
	((type *)tal_alloc_((ctx), sizeof(type), (count), false, #type "[]"))
#define tal_arrz(ctx, type, count) \
	((type *)tal_alloc_((ctx), sizeof(type), (count), true, #type "[]"))

/**
 * tal_resize - change the element count of an array in place.
 * @pp: pointer to the array pointer (it may move).
 * @count: new number of elements.
 */
#define tal_resize(pp, count) \
	tal_resize_((void **)(pp), sizeof(**(pp)), (count))

/**
 * tal_arr_expand - append one element to an array.
 * @pp: pointer to the array pointer.
 * @value: element to append.
 */
#define tal_arr_expand(pp, value)                                       \
	do {                                                            \
		size_t tal_arr_expand_n_ = tal_count(*(pp));            \
		tal_resize((pp), tal_arr_expand_n_ + 1);                \
		(*(pp))[tal_arr_expand_n_] = (value);                   \
	} while (0)

/**
 * tal_free - free an allocation and everything allocated under it.
 *
 * Returns NULL so callers can write `p = tal_free(p);`.
 */
void *tal_free(const tal_t *p);

/**
 * tal_count - number of elements in a tal array (1 for single objects).
 */
size_t tal_count(const tal_t *p);

/**
 * tal_steal - move an allocation to a new parent.
 */
void *tal_steal(const tal_t *new_parent, const tal_t *p);

/**
 * tal_strdup / tal_strndup / tal_fmt - allocate strings under @ctx.
 */
char *tal_strdup(const tal_t *ctx, const char *s);
char *tal_strndup(const tal_t *ctx, const char *s, size_t n);
char *tal_fmt(const tal_t *ctx, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
char *tal_vfmt(const tal_t *ctx, const char *fmt, va_list ap);

/**
 * tal_append_fmt - append formatted text to a tal string, growing it.
 */
void tal_append_fmt(char **s, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/* Implementation helpers used by the macros above; do not call directly. */
void *tal_alloc_(const tal_t *ctx, size_t elemsize, size_t count, bool clear,
		 const char *label);
void tal_resize_(void **pp, size_t elemsize, size_t count);

#endif /* BOLTZMANN_COMMON_TAL_H */
