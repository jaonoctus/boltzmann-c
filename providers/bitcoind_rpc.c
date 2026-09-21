/*
 * bitcoind's JSON-RPC interface.
 *
 * Configured through the environment, as in the reference implementation:
 *   BOLTZMANN_RPC_USERNAME, BOLTZMANN_RPC_PASSWORD,
 *   BOLTZMANN_RPC_HOST, BOLTZMANN_RPC_PORT
 *
 * bitcoind must run with txindex=1: the value and address of each input
 * are only known by looking up the transaction that created it.
 */
#include "common/http.h"
#include "common/utils.h"
#include "providers/provider.h"

#include <stdlib.h>

struct rpc_config {
	char *url;
	char *userpwd;
};

static const char *require_env(const char *name)
{
	const char *val = getenv(name);

	if (!val)
		die("MissingRPCConfigurationError with %s is not set.", name);
	return val;
}

static struct rpc_config *rpc_config_from_env(const tal_t *ctx)
{
	struct rpc_config *cfg = tal(ctx, struct rpc_config);
	const char *user = require_env("BOLTZMANN_RPC_USERNAME");
	const char *pass = require_env("BOLTZMANN_RPC_PASSWORD");
	const char *host = require_env("BOLTZMANN_RPC_HOST");
	const char *port = require_env("BOLTZMANN_RPC_PORT");

	cfg->url = tal_fmt(cfg, "http://%s:%s/", host, port);
	cfg->userpwd = tal_fmt(cfg, "%s:%s", user, pass);
	return cfg;
}

/**
 * rpc_call - one JSON-RPC request; @params is JSON text for the array.
 * Returns the "result" member, or NULL with *err set.
 */
static struct json_value *rpc_call(const tal_t *ctx,
				   const struct rpc_config *cfg,
				   const char *method, const char *params,
				   char **err)
{
	char *req = tal_fmt(ctx, "{\"jsonrpc\":\"1.0\",\"id\":\"boltzmann\",\"method\":\"%s\",\"params\":%s}",
			    method, params);
	char *body = http_post(ctx, cfg->url, cfg->userpwd, req, 30, err);
	struct json_value *resp;
	const struct json_value *error, *result;

	tal_free(req);
	if (!body)
		return NULL;
	resp = json_parse(ctx, body, err);
	tal_free(body);
	if (!resp)
		return NULL;
	error = json_get(resp, "error");
	if (!json_is_null(error)) {
		*err = tal_fmt(ctx, "%s: %s (code %lld)", method,
			       json_get_string(error, "message", "rpc error"),
			       (long long)json_get_s64(error, "code", 0));
		tal_free(resp);
		return NULL;
	}
	result = json_get(resp, "result");
	if (!result) {
		*err = tal_fmt(ctx, "%s: no result", method);
		tal_free(resp);
		return NULL;
	}
	return tal_steal(ctx, (void *)result);
}

static struct json_value *get_decoded_tx(const tal_t *ctx,
					 const struct rpc_config *cfg,
					 const char *txid, char **err)
{
	char *params = tal_fmt(ctx, "[%s, true]", json_escape(ctx, txid));
	struct json_value *tx = rpc_call(ctx, cfg, "getrawtransaction", params, err);

	tal_free(params);
	return tx;
}

/**
 * output_address - the address of a decoded output, or its script hex.
 *
 * Modern bitcoind reports "address"; older versions an "addresses" list,
 * which the reference joins with spaces.
 */
static const char *output_address(const struct json_value *vout)
{
	const struct json_value *spk = json_get(vout, "scriptPubKey");
	const struct json_value *addrs = json_get(spk, "addresses");
	const char *addr = json_get_string(spk, "address", NULL);

	if (addr)
		return addr;
	if (json_len(addrs) > 0) {
		char *joined = tal_strdup(vout, "");
		size_t i;

		for (i = 0; i < json_len(addrs); i++)
			tal_append_fmt(&joined, "%s%s", i ? " " : "",
				       addrs->items[i]->string);
		return joined;
	}
	return json_get_string(spk, "hex", NULL);
}

static struct transaction *rpc_get_tx(const tal_t *ctx,
				      const struct blockchain_provider *provider,
				      const char *txid, bool mainnet, char **err)
{
	struct rpc_config *cfg = rpc_config_from_env(ctx);
	struct json_value *tx;
	const struct json_value *vin, *vout;
	const char *blockhash;
	struct transaction *t;
	size_t i;

	*err = NULL;
	tx = get_decoded_tx(ctx, cfg, txid, err);
	if (!tx)
		return NULL;
	t = transaction_new(ctx, json_get_string(tx, "txid", txid));

	/* Height comes from the block header; unconfirmed stays -1.  The
	 * reference never has a time for RPC transactions.
	 */
	blockhash = json_get_string(tx, "blockhash", NULL);
	if (blockhash) {
		char *params = tal_fmt(ctx, "[%s]", json_escape(ctx, blockhash));
		struct json_value *hdr = rpc_call(ctx, cfg, "getblockheader", params, err);

		if (!hdr)
			return NULL;
		t->height = json_get_s64(hdr, "height", -1);
		tal_free(hdr);
	}

	/* Each input is an output of an earlier transaction: look it up. */
	vin = json_get(tx, "vin");
	for (i = 0; i < json_len(vin); i++) {
		const struct json_value *in = vin->items[i];
		const char *prev_txid = json_get_string(in, "txid", NULL);
		s64 prev_n;
		struct json_value *prev_tx;
		const struct json_value *prev_vout;

		if (!prev_txid) {
			txo_new_unknown(&t->inputs);	/* coinbase */
			continue;
		}
		prev_n = json_get_s64(in, "vout", -1);
		prev_tx = get_decoded_tx(ctx, cfg, prev_txid, err);
		if (!prev_tx)
			return NULL;
		prev_vout = json_get(prev_tx, "vout");
		if (prev_n < 0 || (size_t)prev_n >= json_len(prev_vout)) {
			*err = tal_fmt(ctx, "PrevOutAddressCannotBeDecodedError with Missing element for vout in tx id %s and output index %lld", prev_txid, (long long)prev_n);
			return NULL;
		}
		txo_new(&t->inputs, (int)prev_n,
			json_btc_to_sat(json_get(prev_vout->items[prev_n], "value")),
			output_address(prev_vout->items[prev_n]));
		tal_free(prev_tx);
	}

	vout = json_get(tx, "vout");
	for (i = 0; i < json_len(vout); i++) {
		const struct json_value *out = vout->items[i];

		txo_new(&t->outputs, (int)json_get_s64(out, "n", (s64)i),
			json_btc_to_sat(json_get(out, "value")), output_address(out));
	}
	tal_free(tx);
	return t;
}

struct blockchain_provider *bitcoind_rpc_provider(const tal_t *ctx)
{
	struct blockchain_provider *p = talz(ctx, struct blockchain_provider);

	p->description = "local RPC interface";
	p->get_tx = rpc_get_tx;
	return p;
}
