#ifndef BOLTZMANN_PROVIDERS_PROVIDER_H
#define BOLTZMANN_PROVIDERS_PROVIDER_H
/*
 * Blockchain data providers.
 *
 * A provider knows how to turn a txid into a struct transaction.  Every
 * provider is a small file translating one API's JSON into the common
 * model; the analysis never sees where the data came from.
 */
#include "boltzmann/transaction.h"
#include "common/json.h"
#include "common/tal.h"

struct blockchain_provider {
	/* Shown in the "DEBUG: Using ..." line. */
	const char *description;

	/**
	 * get_tx - fetch @txid.  Returns NULL and sets *err on failure.
	 * @mainnet: false selects the testnet endpoint where there is one.
	 */
	struct transaction *(*get_tx)(const tal_t *ctx,
				      const struct blockchain_provider *provider,
				      const char *txid, bool mainnet, char **err);

	/* Provider-specific configuration. */
	const char *base_url;		/* esplora: API root */
	const char *path;		/* file: path to read */
	const char *inputs;		/* inline: "AMOUNT|LABEL:AMOUNT,..." */
	const char *outputs;		/* inline: same, for the outputs */
};

/* HTTP timeout used by every remote provider, as in the reference. */
#define PROVIDER_TIMEOUT_SECONDS 10

/* Constructors. */
struct blockchain_provider *blockchain_info_provider(const tal_t *ctx);
struct blockchain_provider *blockstream_provider(const tal_t *ctx);
struct blockchain_provider *mempool_space_provider(const tal_t *ctx);
struct blockchain_provider *bitcoind_rpc_provider(const tal_t *ctx);
struct blockchain_provider *file_provider(const tal_t *ctx, const char *path);
struct blockchain_provider *inline_provider(const tal_t *ctx,
					    const char *inputs,
					    const char *outputs);

/*
 * JSON translators, shared with the file provider which accepts any of
 * these formats.
 */

/**
 * transaction_from_bci_json - blockchain.info /rawtx format:
 * {hash, block_height, time, inputs:[{prev_out:{n,value,addr,tx_index}}],
 *  out:[{n,value,addr,tx_index}]}
 */
struct transaction *transaction_from_bci_json(const tal_t *ctx,
					      const struct json_value *tx,
					      char **err);

/**
 * transaction_from_esplora_json - Blockstream / mempool.space /tx format:
 * {txid, status:{block_height,block_time},
 *  vin:[{vout, prevout:{value, scriptpubkey_address, scriptpubkey}}],
 *  vout:[{value, scriptpubkey_address, scriptpubkey}]}
 */
struct transaction *transaction_from_esplora_json(const tal_t *ctx,
						  const struct json_value *tx,
						  char **err);

#endif /* BOLTZMANN_PROVIDERS_PROVIDER_H */
