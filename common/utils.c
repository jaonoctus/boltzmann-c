#include "common/utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void stable_sort(void *base, size_t count, size_t size,
		 int (*cmp)(const void *, const void *))
{
	char *arr = base;
	char tmp[256];
	size_t i, j;

	if (size > sizeof(tmp))
		die("stable_sort: element size %zu too large", size);

	for (i = 1; i < count; i++) {
		memcpy(tmp, arr + i * size, size);
		/* Shift larger elements right; equal ones stay put. */
		for (j = i; j > 0 && cmp(arr + (j - 1) * size, tmp) > 0; j--)
			memcpy(arr + j * size, arr + (j - 1) * size, size);
		memcpy(arr + j * size, tmp, size);
	}
}

double time_now_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

bool streq(const char *a, const char *b)
{
	if (!a || !b)
		return a == b;
	return strcmp(a, b) == 0;
}
