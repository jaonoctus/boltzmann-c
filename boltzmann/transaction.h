#ifndef BOLTZMANN_TRANSACTION_H
#define BOLTZMANN_TRANSACTION_H
/*
 * The transaction model: the few fields of a Bitcoin transaction that the
 * analysis needs.  Every data provider (blockchain.info, Esplora, bitcoind,
 * a local file) is translated into this one structure.
 */
#include "common/tal.h"
#include "common/types.h"

/**
 * struct txo - one transaction output, seen either as an input (a previous
 * output being spent) or as an output of the analysed transaction.
 *
 * @n: index of the txo in the transaction that created it, -1 if unknown.
 * @value: amount in satoshis, -1 if unknown (coinbase inputs).
 * @address: address, or the scriptpubkey hex when no address exists,
 *           or NULL when the provider gave neither.
 * @has_tx_idx / @tx_idx: blockchain.info's internal transaction index;
 *           other providers don't have one.  Only ever printed.
 */
struct txo {
	int n;
	s64 value;
	const char *address;
	bool has_tx_idx;
	s64 tx_idx;
};

/**
 * struct transaction - the analysed transaction.
 *
 * @height: block height, -1 if unknown/unconfirmed.
 * @has_time / @time: block or first-seen time; not every provider has it.
 * @txid: hex transaction id.
 * @inputs / @outputs: tal arrays of txo pointers.
 */
struct transaction {
	s64 height;
	bool has_time;
	s64 time;
	const char *txid;
	struct txo **inputs;
	struct txo **outputs;
};

/**
 * transaction_new - allocate an empty transaction.
 */
struct transaction *transaction_new(const tal_t *ctx, const char *txid);

/**
 * txo_new - allocate a txo and append it to @list (a tal array).
 */
struct txo *txo_new(struct txo ***list, int n, s64 value,
		    const char *address);

/**
 * txo_new_unknown - a placeholder for a coinbase input: no value, no address.
 */
struct txo *txo_new_unknown(struct txo ***list);

/**
 * transaction_str - the debug representation printed after fetching a tx.
 *
 * Reproduces the Python Transaction.__str__ format character for character.
 */
char *transaction_str(const tal_t *ctx, const struct transaction *tx);

#endif /* BOLTZMANN_TRANSACTION_H */
