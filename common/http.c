#include "common/http.h"

#include <curl/curl.h>
#include <string.h>

struct body {
	char *data;	/* tal string, grown as data arrives */
};

static size_t on_data(char *chunk, size_t size, size_t nmemb, void *arg)
{
	struct body *body = arg;
	size_t oldlen = strlen(body->data), add = size * nmemb;

	tal_resize(&body->data, oldlen + add + 1);
	memcpy(body->data + oldlen, chunk, add);
	body->data[oldlen + add] = '\0';
	return add;
}

static char *perform(const tal_t *ctx, const char *url, const char *userpwd,
		     const char *post_body, long timeout_seconds, char **err)
{
	CURL *curl;
	CURLcode rc;
	long status = 0;
	struct body body;
	struct curl_slist *headers = NULL;

	body.data = tal_strdup(ctx, "");
	curl = curl_easy_init();
	if (!curl) {
		*err = tal_strdup(ctx, "curl_easy_init failed");
		return tal_free(body.data);
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");	/* gzip ok */
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "boltzmann-c/1.0");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	if (userpwd)
		curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
	if (post_body) {
		headers = curl_slist_append(headers, "Content-Type: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
	}

	rc = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		*err = tal_fmt(ctx, "%s: %s", url, curl_easy_strerror(rc));
		return tal_free(body.data);
	}
	if (status < 200 || status >= 300) {
		*err = tal_fmt(ctx, "%s: HTTP %ld: %s", url, status, body.data);
		return tal_free(body.data);
	}
	return body.data;
}

char *http_get(const tal_t *ctx, const char *url, long timeout_seconds,
	       char **err)
{
	return perform(ctx, url, NULL, NULL, timeout_seconds, err);
}

char *http_post(const tal_t *ctx, const char *url, const char *userpwd,
		const char *body, long timeout_seconds, char **err)
{
	return perform(ctx, url, userpwd, body, timeout_seconds, err);
}
