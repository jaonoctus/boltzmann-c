#ifndef BOLTZMANN_COMMON_HTTP_H
#define BOLTZMANN_COMMON_HTTP_H
/*
 * Thin libcurl wrapper: fetch a URL, get the body back as a string.
 */
#include "common/tal.h"

/**
 * http_get - GET @url.  Returns the body, or NULL with *err set.
 * Non-2xx responses are errors; compressed responses are decoded.
 */
char *http_get(const tal_t *ctx, const char *url, long timeout_seconds,
	       char **err);

/**
 * http_post - POST @body to @url with optional "user:password" basic auth.
 */
char *http_post(const tal_t *ctx, const char *url, const char *userpwd,
		const char *body, long timeout_seconds, char **err);

#endif /* BOLTZMANN_COMMON_HTTP_H */
