#ifndef BOLTZMANN_COMMON_UTILS_H
#define BOLTZMANN_COMMON_UTILS_H
#include "common/types.h"

#include <stddef.h>

/**
 * die - print a message to stderr and exit(1).
 */
void die(const char *fmt, ...)
	__attribute__((format(printf, 1, 2), noreturn));

/**
 * stable_sort - insertion sort, kept because it is obviously stable.
 *
 * Python's sorted() is stable and the reference implementation relies on
 * that: txos with equal amounts keep their relative order, which decides
 * the row/column order of the printed matrix.  qsort() gives no such
 * guarantee, and the arrays sorted here never exceed a few dozen entries.
 */
void stable_sort(void *base, size_t count, size_t size,
		 int (*cmp)(const void *, const void *));

/**
 * time_now_seconds - monotonic wall-clock seconds, for the duration limit.
 */
double time_now_seconds(void);

/**
 * streq - true if two strings are equal (either may be NULL).
 */
bool streq(const char *a, const char *b);

#endif /* BOLTZMANN_COMMON_UTILS_H */
