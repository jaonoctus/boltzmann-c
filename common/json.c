#include "common/json.h"
#include "common/utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct parser {
	const tal_t *ctx;
	const char *p;
	char *err;
};

static struct json_value *parse_value(struct parser *ps);

static void skip_ws(struct parser *ps)
{
	while (isspace((unsigned char)*ps->p))
		ps->p++;
}

static struct json_value *fail(struct parser *ps, const char *what)
{
	if (!ps->err)
		ps->err = tal_fmt(ps->ctx, "JSON error: %s near '%.20s'", what, ps->p);
	return NULL;
}

static struct json_value *new_value(struct parser *ps, enum json_type type)
{
	struct json_value *v = talz(ps->ctx, struct json_value);

	v->type = type;
	return v;
}

/* Parses one \uXXXX escape into UTF-8, handling surrogate pairs. */
static bool parse_unicode_escape(struct parser *ps, char **out)
{
	u32 cp = 0, i;

	for (i = 0; i < 4; i++) {
		char c = ps->p[i];

		if (!isxdigit((unsigned char)c))
			return false;
		cp = cp * 16 + (u32)(isdigit((unsigned char)c) ? c - '0'
						: tolower(c) - 'a' + 10);
	}
	ps->p += 4;
	if (cp >= 0xD800 && cp <= 0xDBFF && ps->p[0] == '\\' && ps->p[1] == 'u') {
		u32 lo = 0;

		ps->p += 2;
		for (i = 0; i < 4; i++) {
			char c = ps->p[i];

			if (!isxdigit((unsigned char)c))
				return false;
			lo = lo * 16 + (u32)(isdigit((unsigned char)c) ? c - '0'
							: tolower(c) - 'a' + 10);
		}
		ps->p += 4;
		cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
	}
	if (cp < 0x80) {
		tal_append_fmt(out, "%c", (char)cp);
	} else if (cp < 0x800) {
		tal_append_fmt(out, "%c%c", (char)(0xC0 | (cp >> 6)),
			       (char)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		tal_append_fmt(out, "%c%c%c", (char)(0xE0 | (cp >> 12)),
			       (char)(0x80 | ((cp >> 6) & 0x3F)),
			       (char)(0x80 | (cp & 0x3F)));
	} else {
		tal_append_fmt(out, "%c%c%c%c", (char)(0xF0 | (cp >> 18)),
			       (char)(0x80 | ((cp >> 12) & 0x3F)),
			       (char)(0x80 | ((cp >> 6) & 0x3F)),
			       (char)(0x80 | (cp & 0x3F)));
	}
	return true;
}

static char *parse_string_literal(struct parser *ps)
{
	char *out = tal_strdup(ps->ctx, "");
	const char *start;

	if (*ps->p != '"') {
		fail(ps, "expected string");
		return NULL;
	}
	ps->p++;
	start = ps->p;
	while (*ps->p && *ps->p != '"') {
		if (*ps->p != '\\') {
			ps->p++;
			continue;
		}
		/* Flush the plain run, then decode the escape. */
		tal_append_fmt(&out, "%.*s", (int)(ps->p - start), start);
		ps->p++;
		switch (*ps->p) {
		case '"':
			tal_append_fmt(&out, "\"");
			break;
		case '\\':
			tal_append_fmt(&out, "\\");
			break;
		case '/':
			tal_append_fmt(&out, "/");
			break;
		case 'b':
			tal_append_fmt(&out, "\b");
			break;
		case 'f':
			tal_append_fmt(&out, "\f");
			break;
		case 'n':
			tal_append_fmt(&out, "\n");
			break;
		case 'r':
			tal_append_fmt(&out, "\r");
			break;
		case 't':
			tal_append_fmt(&out, "\t");
			break;
		case 'u':
			ps->p++;
			if (!parse_unicode_escape(ps, &out)) {
				fail(ps, "bad \\u escape");
				return NULL;
			}
			start = ps->p;
			continue;
		default:
			fail(ps, "bad escape");
			return NULL;
		}
		ps->p++;
		start = ps->p;
	}
	if (*ps->p != '"') {
		fail(ps, "unterminated string");
		return NULL;
	}
	tal_append_fmt(&out, "%.*s", (int)(ps->p - start), start);
	ps->p++;
	return out;
}

static struct json_value *parse_number(struct parser *ps)
{
	const char *start = ps->p;
	struct json_value *v;

	if (*ps->p == '-')
		ps->p++;
	while (isdigit((unsigned char)*ps->p) || *ps->p == '.' || *ps->p == 'e' ||
	       *ps->p == 'E' || *ps->p == '+' || *ps->p == '-')
		ps->p++;
	if (ps->p == start)
		return fail(ps, "expected number");
	v = new_value(ps, JSON_NUMBER);
	v->number = tal_strndup(v, start, (size_t)(ps->p - start));
	return v;
}

static struct json_value *parse_array(struct parser *ps)
{
	struct json_value *v = new_value(ps, JSON_ARRAY);

	v->items = tal_arr(v, struct json_value *, 0);
	ps->p++;			/* '[' */
	skip_ws(ps);
	if (*ps->p == ']') {
		ps->p++;
		return v;
	}
	for (;;) {
		struct json_value *item = parse_value(ps);

		if (!item)
			return NULL;
		tal_steal(v->items, item);
		tal_arr_expand(&v->items, item);
		skip_ws(ps);
		if (*ps->p == ',') {
			ps->p++;
			continue;
		}
		if (*ps->p == ']') {
			ps->p++;
			return v;
		}
		return fail(ps, "expected ',' or ']'");
	}
}

static struct json_value *parse_object(struct parser *ps)
{
	struct json_value *v = new_value(ps, JSON_OBJECT);

	v->keys = tal_arr(v, const char *, 0);
	v->values = tal_arr(v, struct json_value *, 0);
	ps->p++;			/* '{' */
	skip_ws(ps);
	if (*ps->p == '}') {
		ps->p++;
		return v;
	}
	for (;;) {
		char *key;
		struct json_value *val;

		skip_ws(ps);
		key = parse_string_literal(ps);
		if (!key)
			return NULL;
		skip_ws(ps);
		if (*ps->p != ':')
			return fail(ps, "expected ':'");
		ps->p++;
		val = parse_value(ps);
		if (!val)
			return NULL;
		tal_steal(v->keys, key);
		tal_steal(v->values, val);
		tal_arr_expand(&v->keys, key);
		tal_arr_expand(&v->values, val);
		skip_ws(ps);
		if (*ps->p == ',') {
			ps->p++;
			continue;
		}
		if (*ps->p == '}') {
			ps->p++;
			return v;
		}
		return fail(ps, "expected ',' or '}'");
	}
}

static struct json_value *parse_value(struct parser *ps)
{
	struct json_value *v;

	skip_ws(ps);
	switch (*ps->p) {
	case '{':
		return parse_object(ps);
	case '[':
		return parse_array(ps);
	case '"':
		v = new_value(ps, JSON_STRING);
		v->string = parse_string_literal(ps);
		if (!v->string)
			return NULL;
		tal_steal(v, (void *)v->string);
		return v;
	case 't':
		if (strncmp(ps->p, "true", 4) != 0)
			return fail(ps, "bad literal");
		ps->p += 4;
		v = new_value(ps, JSON_BOOL);
		v->boolean = true;
		return v;
	case 'f':
		if (strncmp(ps->p, "false", 5) != 0)
			return fail(ps, "bad literal");
		ps->p += 5;
		return new_value(ps, JSON_BOOL);
	case 'n':
		if (strncmp(ps->p, "null", 4) != 0)
			return fail(ps, "bad literal");
		ps->p += 4;
		return new_value(ps, JSON_NULL);
	default:
		return parse_number(ps);
	}
}

struct json_value *json_parse(const tal_t *ctx, const char *text, char **err)
{
	struct parser ps;
	struct json_value *v;

	ps.ctx = ctx;
	ps.p = text;
	ps.err = NULL;
	v = parse_value(&ps);
	if (v) {
		skip_ws(&ps);
		if (*ps.p != '\0') {
			fail(&ps, "trailing characters");
			v = NULL;
		}
	}
	if (!v && err)
		*err = ps.err;
	return v;
}

const struct json_value *json_get(const struct json_value *obj, const char *key)
{
	size_t i;

	if (!obj || obj->type != JSON_OBJECT)
		return NULL;
	for (i = 0; i < tal_count(obj->keys); i++) {
		if (streq(obj->keys[i], key))
			return obj->values[i];
	}
	return NULL;
}

const char *json_get_string(const struct json_value *obj, const char *key,
			    const char *dflt)
{
	const struct json_value *v = json_get(obj, key);

	return (v && v->type == JSON_STRING) ? v->string : dflt;
}

s64 json_get_s64(const struct json_value *obj, const char *key, s64 dflt)
{
	const struct json_value *v = json_get(obj, key);

	return (v && v->type == JSON_NUMBER) ? json_to_s64(v) : dflt;
}

size_t json_len(const struct json_value *arr)
{
	return (arr && arr->type == JSON_ARRAY) ? tal_count(arr->items) : 0;
}

bool json_is_null(const struct json_value *v)
{
	return !v || v->type == JSON_NULL;
}

s64 json_to_s64(const struct json_value *v)
{
	if (!v || v->type != JSON_NUMBER)
		return 0;
	if (strchr(v->number, '.') || strchr(v->number, 'e') || strchr(v->number, 'E'))
		return (s64)strtod(v->number, NULL);
	return strtoll(v->number, NULL, 10);
}

s64 json_btc_to_sat(const struct json_value *v)
{
	const char *p;
	s64 whole = 0, frac = 0;
	int frac_digits = 0;
	bool negative = false;

	if (!v || v->type != JSON_NUMBER)
		return 0;
	p = v->number;
	if (strchr(p, 'e') || strchr(p, 'E'))
		return (s64)(strtod(p, NULL) * 1e8 + 0.5);
	if (*p == '-') {
		negative = true;
		p++;
	}
	while (isdigit((unsigned char)*p))
		whole = whole * 10 + (*p++ - '0');
	if (*p == '.') {
		p++;
		while (isdigit((unsigned char)*p) && frac_digits < 8) {
			frac = frac * 10 + (*p++ - '0');
			frac_digits++;
		}
	}
	while (frac_digits < 8) {
		frac *= 10;
		frac_digits++;
	}
	return (negative ? -1 : 1) * (whole * 100000000LL + frac);
}

char *json_escape(const tal_t *ctx, const char *s)
{
	char *out = tal_strdup(ctx, "\"");

	for (; *s; s++) {
		switch (*s) {
		case '"':
			tal_append_fmt(&out, "\\\"");
			break;
		case '\\':
			tal_append_fmt(&out, "\\\\");
			break;
		case '\n':
			tal_append_fmt(&out, "\\n");
			break;
		case '\r':
			tal_append_fmt(&out, "\\r");
			break;
		case '\t':
			tal_append_fmt(&out, "\\t");
			break;
		default:
			if ((unsigned char)*s < 0x20)
				tal_append_fmt(&out, "\\u%04x", (unsigned char)*s);
			else
				tal_append_fmt(&out, "%c", *s);
		}
	}
	tal_append_fmt(&out, "\"");
	return out;
}
