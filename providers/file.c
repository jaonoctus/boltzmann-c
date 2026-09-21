/*
 * A transaction read from a local JSON file, in either the blockchain.info
 * or the Esplora format.  Handy for offline analysis and for testing.
 * The path "-" reads standard input, so a fetch can be piped straight in.
 */
#include "common/utils.h"
#include "providers/provider.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static char *read_file(const tal_t *ctx, const char *path, char **err)
{
	bool is_stdin = streq(path, "-");
	FILE *f = is_stdin ? stdin : fopen(path, "rb");
	char *data = tal_arr(ctx, char, 0);
	char buf[4096];
	size_t n;

	if (!f) {
		*err = tal_fmt(ctx, "%s: %s", path, strerror(errno));
		return tal_free(data);
	}
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		size_t old = tal_count(data);

		tal_resize(&data, old + n);
		memcpy(data + old, buf, n);
	}
	if (!is_stdin)
		fclose(f);
	tal_resize(&data, tal_count(data) + 1);
	data[tal_count(data) - 1] = '\0';
	return data;
}

static struct transaction *file_get_tx(const tal_t *ctx,
				       const struct blockchain_provider *provider,
				       const char *txid, bool mainnet, char **err)
{
	char *text;
	struct json_value *json;
	struct transaction *tx;

	*err = NULL;
	text = read_file(ctx, provider->path, err);
	if (!text)
		return NULL;
	if (text[strspn(text, " \t\r\n")] == '\0') {
		*err = tal_fmt(ctx, "%s: no input", provider->path);
		return tal_free(text);
	}
	json = json_parse(ctx, text, err);
	tal_free(text);
	if (!json)
		return NULL;

	/* Tell the formats apart by their top-level keys. */
	if (json_get(json, "out") && json_get(json, "inputs")) {
		tx = transaction_from_bci_json(ctx, json, err);
	} else if (json_get(json, "vin") && json_get(json, "vout")) {
		tx = transaction_from_esplora_json(ctx, json, err);
	} else {
		*err = tal_fmt(ctx, "%s: unrecognised transaction format (expected blockchain.info or Esplora JSON)",
			       provider->path);
		tx = NULL;
	}
	tal_free(json);
	return tx;
}

struct blockchain_provider *file_provider(const tal_t *ctx, const char *path)
{
	struct blockchain_provider *p = talz(ctx, struct blockchain_provider);

	p->description = streq(path, "-") ? "standard input" : "local file";
	p->get_tx = file_get_tx;
	p->path = tal_strdup(p, path);
	return p;
}
