#include "common/tal.h"
#include "common/utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each allocation is preceded by this header.  Children form a doubly
 * linked list so that freeing or moving one child is O(1).
 */
struct tal_hdr {
	struct tal_hdr *parent;
	struct tal_hdr *first_child;
	struct tal_hdr *prev, *next;	/* siblings */
	size_t elemsize;
	size_t count;
	const char *label;		/* type name, for debugging */
	size_t pad_;			/* keeps the payload 16-byte aligned */
};

static struct tal_hdr *to_hdr(const tal_t *p)
{
	return (struct tal_hdr *)p - 1;
}

static tal_t *from_hdr(struct tal_hdr *h)
{
	return h + 1;
}

static void link_child(struct tal_hdr *parent, struct tal_hdr *child)
{
	child->parent = parent;
	child->prev = NULL;
	child->next = parent ? parent->first_child : NULL;
	if (child->next)
		child->next->prev = child;
	if (parent)
		parent->first_child = child;
}

static void unlink_child(struct tal_hdr *child)
{
	if (child->prev)
		child->prev->next = child->next;
	else if (child->parent)
		child->parent->first_child = child->next;
	if (child->next)
		child->next->prev = child->prev;
	child->parent = NULL;
	child->prev = NULL;
	child->next = NULL;
}

void *tal_alloc_(const tal_t *ctx, size_t elemsize, size_t count, bool clear,
		 const char *label)
{
	struct tal_hdr *h;
	size_t bytes = elemsize * count;

	/* Always allocate at least one byte so distinct arrays have
	 * distinct addresses.
	 */
	h = malloc(sizeof(*h) + (bytes ? bytes : 1));
	if (!h)
		die("out of memory allocating %zu bytes for %s", bytes, label);
	h->first_child = NULL;
	h->elemsize = elemsize;
	h->count = count;
	h->label = label;
	link_child(ctx ? to_hdr(ctx) : NULL, h);
	if (clear)
		memset(from_hdr(h), 0, bytes);
	return from_hdr(h);
}

void *tal_free(const tal_t *p)
{
	struct tal_hdr *h;

	if (!p)
		return NULL;
	h = to_hdr(p);
	while (h->first_child)
		tal_free(from_hdr(h->first_child));
	unlink_child(h);
	free(h);
	return NULL;
}

size_t tal_count(const tal_t *p)
{
	return p ? to_hdr(p)->count : 0;
}

void *tal_steal(const tal_t *new_parent, const tal_t *p)
{
	struct tal_hdr *h = to_hdr(p);

	unlink_child(h);
	link_child(new_parent ? to_hdr(new_parent) : NULL, h);
	return (void *)p;
}

void tal_resize_(void **pp, size_t elemsize, size_t count)
{
	struct tal_hdr *old = to_hdr(*pp), *h;
	struct tal_hdr *parent = old->parent, *prev = old->prev, *next = old->next;
	struct tal_hdr *child;
	size_t bytes = elemsize * count;

	assert(old->elemsize == elemsize);
	h = realloc(old, sizeof(*h) + (bytes ? bytes : 1));
	if (!h)
		die("out of memory resizing %s to %zu elements", old->label, count);
	h->count = count;
	if (h == old)
		return;

	/* The block moved: everyone who pointed at the old header must be
	 * told about the new address.
	 */
	if (prev)
		prev->next = h;
	else if (parent)
		parent->first_child = h;
	if (next)
		next->prev = h;
	for (child = h->first_child; child; child = child->next)
		child->parent = h;
	*pp = from_hdr(h);
}

char *tal_strndup(const tal_t *ctx, const char *s, size_t n)
{
	char *ret = tal_arr(ctx, char, n + 1);

	memcpy(ret, s, n);
	ret[n] = '\0';
	return ret;
}

char *tal_strdup(const tal_t *ctx, const char *s)
{
	return tal_strndup(ctx, s, strlen(s));
}

char *tal_vfmt(const tal_t *ctx, const char *fmt, va_list ap)
{
	va_list ap2;
	int len;
	char *ret;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	assert(len >= 0);

	ret = tal_arr(ctx, char, (size_t)len + 1);
	vsnprintf(ret, (size_t)len + 1, fmt, ap);
	return ret;
}

char *tal_fmt(const tal_t *ctx, const char *fmt, ...)
{
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = tal_vfmt(ctx, fmt, ap);
	va_end(ap);
	return ret;
}

void tal_append_fmt(char **s, const char *fmt, ...)
{
	va_list ap, ap2;
	size_t oldlen = strlen(*s);
	int len;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	assert(len >= 0);

	tal_resize(s, oldlen + (size_t)len + 1);
	vsnprintf(*s + oldlen, (size_t)len + 1, fmt, ap);
	va_end(ap);
}
