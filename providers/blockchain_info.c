/*
 * blockchain.info: the reference implementation's default data source.
 */
#include "common/http.h"
#include "common/utils.h"
#include "providers/provider.h"

/* One txo in BCI's format; @txo may be NULL for a coinbase input. */
static void add_bci_txo(struct txo ***list, const struct json_value *txo,
			char **err)
{
	const char *address;
	struct txo *t;

	if (json_is_null(txo)) {
		txo_new_unknown(list);
		return;
	}
	/* Address if there is one, otherwise the raw script. */
	address = json_get_string(txo, "addr", NULL);
	if (!address)
		address = json_get_string(txo, "script", NULL);
	if (!address) {
		if (!*err)
			*err = tal_strdup(*list, "Could not assign address to txo");
		return;
	}
	t = txo_new(list, (int)json_get_s64(txo, "n", -1),
		    json_get_s64(txo, "value", -1), address);
	if (json_get(txo, "tx_index") && !json_is_null(json_get(txo, "tx_index"))) {
		t->has_tx_idx = true;
		t->tx_idx = json_get_s64(txo, "tx_index", -1);
	} else {
		/* Python prints None for a missing tx_index. */
		t->has_tx_idx = false;
	}
}

struct transaction *transaction_from_bci_json(const tal_t *ctx,
					      const struct json_value *tx,
					      char **err)
{
	const char *txid = json_get_string(tx, "hash", NULL);
	const struct json_value *ins = json_get(tx, "inputs");
	const struct json_value *outs = json_get(tx, "out");
	struct transaction *t;
	size_t i;

	if (!txid || !ins || !outs) {
		*err = tal_strdup(ctx, "not a blockchain.info transaction");
		return NULL;
	}
	t = transaction_new(ctx, txid);
	t->height = json_get_s64(tx, "block_height", -1);
	if (json_get(tx, "time") && !json_is_null(json_get(tx, "time"))) {
		t->has_time = true;
		t->time = json_get_s64(tx, "time", 0);
	}
	for (i = 0; i < json_len(ins); i++)
		add_bci_txo(&t->inputs, json_get(ins->items[i], "prev_out"), err);
	for (i = 0; i < json_len(outs); i++)
		add_bci_txo(&t->outputs, outs->items[i], err);
	if (*err) {
		*err = tal_steal(ctx, *err);
		return tal_free(t);
	}
	return t;
}

static struct transaction *bci_get_tx(const tal_t *ctx,
				      const struct blockchain_provider *provider,
				      const char *txid, bool mainnet, char **err)
{
	const char *base = mainnet ? "https://blockchain.info/"
				   : "https://testnet.blockchain.info/";
	char *url = tal_fmt(ctx, "%srawtx/%s", base, txid);
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
	tx = transaction_from_bci_json(ctx, json, err);
	tal_free(json);
	tal_free(body);
	tal_free(url);
	return tx;
}

struct blockchain_provider *blockchain_info_provider(const tal_t *ctx)
{
	struct blockchain_provider *p = talz(ctx, struct blockchain_provider);

	p->description = "remote blockchain.info API";
	p->get_tx = bci_get_tx;
	return p;
}
