#ifndef BOLTZMANN_COMMON_JSON_H
#define BOLTZMANN_COMMON_JSON_H
/*
 * A small JSON parser: enough to read the responses of the blockchain
 * APIs and bitcoind.  The whole document is parsed into a tree of
 * struct json_value allocated under one tal context.
 */
#include "common/tal.h"
#include "common/types.h"

enum json_type {
	JSON_NULL,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
};

struct json_value {
	enum json_type type;
	bool boolean;			/* JSON_BOOL */
	const char *number;		/* JSON_NUMBER: the literal text */
	const char *string;		/* JSON_STRING: decoded */
	struct json_value **items;	/* JSON_ARRAY */
	const char **keys;		/* JSON_OBJECT: parallel to values */
	struct json_value **values;
};

/**
 * json_parse - parse a document.  Returns NULL and sets *err on failure.
 */
struct json_value *json_parse(const tal_t *ctx, const char *text, char **err);

/**
 * json_get - member of an object, or NULL if absent or not an object.
 */
const struct json_value *json_get(const struct json_value *obj,
				  const char *key);

/**
 * json_get_string / json_get_s64 - typed member access with a default.
 */
const char *json_get_string(const struct json_value *obj, const char *key,
			    const char *dflt);
s64 json_get_s64(const struct json_value *obj, const char *key, s64 dflt);

/**
 * json_len - number of array items (0 if not an array).
 */
size_t json_len(const struct json_value *arr);

/**
 * json_is_null - true for a missing value or an explicit null.
 */
bool json_is_null(const struct json_value *v);

/**
 * json_to_s64 - integer value of a number (truncating a fraction).
 */
s64 json_to_s64(const struct json_value *v);

/**
 * json_btc_to_sat - a decimal BTC amount ("50.00000000") in satoshis,
 * read from the literal text so no float rounding is involved.
 */
s64 json_btc_to_sat(const struct json_value *v);

/**
 * json_escape - quote a string as a JSON literal (used to build requests).
 */
char *json_escape(const tal_t *ctx, const char *s);

#endif /* BOLTZMANN_COMMON_JSON_H */
