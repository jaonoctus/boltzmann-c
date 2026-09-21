/*
 * Esplora-compatible APIs: Blockstream and mempool.space serve the same
 * JSON, so one translator covers both.
 */
#include "common/http.h"
#include "common/utils.h"
#include "providers/provider.h"

/* A txo in Esplora format.  For inputs @txo is the "prevout" object. */
static void add_esplora_txo(struct txo ***list, const struct json_value *txo,
			    int n)
{
	const char *address;

	if (json_is_null(txo)) {
		/* Coinbase input: nothing is known about it. */
		txo_new_unknown(list);
		return;
	}
	/* Address if there is one, otherwise the raw script (the Python
	 * code gives up here; falling back keeps OP_RETURN outputs
	 * harmless, they carry no value anyway).
	 */
	address = json_get_string(txo, "scriptpubkey_address", NULL);
	if (!address)
		address = json_get_string(txo, "scriptpubkey", NULL);
	txo_new(list, n, json_get_s64(txo, "value", -1), address);
}

struct transaction *transaction_from_esplora_json(const tal_t *ctx,
						  const struct json_value *tx,
						  char **err)
{
	const char *txid = json_get_string(tx, "txid", NULL);
	const struct json_value *status = json_get(tx, "status");
	const struct json_value *vin = json_get(tx, "vin");
	const struct json_value *vout = json_get(tx, "vout");
	struct transaction *t;
	size_t i;

	if (!txid || !vin || !vout) {
		*err = tal_strdup(ctx, "not an Esplora transaction");
		return NULL;
	}
	t = transaction_new(ctx, txid);
	if (!json_is_null(json_get(status, "block_height")))
		t->height = json_get_s64(status, "block_height", -1);
	if (!json_is_null(json_get(status, "block_time"))) {
		t->has_time = true;
		t->time = json_get_s64(status, "block_time", 0);
	}
	/* Inputs are labelled with the index of the output they spend;
	 * outputs get -1, as the reference implementation does.
	 */
	for (i = 0; i < json_len(vin); i++)
		add_esplora_txo(&t->inputs, json_get(vin->items[i], "prevout"),
				(int)json_get_s64(vin->items[i], "vout", -1));
	for (i = 0; i < json_len(vout); i++)
		add_esplora_txo(&t->outputs, vout->items[i], -1);
	return t;
}

static struct transaction *esplora_get_tx(const tal_t *ctx,
					  const struct blockchain_provider *provider,
					  const char *txid, bool mainnet, char **err)
{
	/* https://blockstream.info/api/tx/<txid>, or /testnet/api/tx/<txid> */
	char *url = tal_fmt(ctx, "%s%s/api/tx/%s", provider->base_url,
			    mainnet ? "" : "/testnet", txid);
	char *body;
	struct json_value *json;
	struct transaction *tx;

	*err = NULL;
	body = http_get(ctx, url, PROVIDER_TIMEOUT_SECONDS, err);
	if (!body)
		return NULL;
	json = json_parse(ctx, body, err);
	if (!json)
		return NULL;
	tx = transaction_from_esplora_json(ctx, json, err);
	tal_free(json);
	tal_free(body);
	tal_free(url);
	return tx;
}

struct blockchain_provider *blockstream_provider(const tal_t *ctx)
{
	struct blockchain_provider *p = talz(ctx, struct blockchain_provider);

	p->description = "remote Blockstream API";
	p->get_tx = esplora_get_tx;
	p->base_url = "https://blockstream.info";
	return p;
}

struct blockchain_provider *mempool_space_provider(const tal_t *ctx)
{
	struct blockchain_provider *p = talz(ctx, struct blockchain_provider);

	p->description = "remote mempool.space API";
	p->get_tx = esplora_get_tx;
	p->base_url = "https://mempool.space";
	return p;
}
