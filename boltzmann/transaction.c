#include "boltzmann/transaction.h"

struct transaction *transaction_new(const tal_t *ctx, const char *txid)
{
	struct transaction *tx = tal(ctx, struct transaction);

	tx->height = -1;
	tx->has_time = false;
	tx->time = 0;
	tx->txid = tal_strdup(tx, txid);
	tx->inputs = tal_arr(tx, struct txo *, 0);
	tx->outputs = tal_arr(tx, struct txo *, 0);
	return tx;
}

struct txo *txo_new(struct txo ***list, int n, s64 value, const char *address)
{
	struct txo *txo = tal(*list, struct txo);

	txo->n = n;
	txo->value = value;
	txo->address = address ? tal_strdup(txo, address) : NULL;
	txo->has_tx_idx = false;
	txo->tx_idx = 0;
	/* The txo lives under the array, so freeing the array frees it. */
	tal_arr_expand(list, txo);
	return txo;
}

struct txo *txo_new_unknown(struct txo ***list)
{
	/* Python's Txo(None): n = -1, value = -1, address = '', tx_idx = -1. */
	struct txo *txo = txo_new(list, -1, -1, "");

	txo->has_tx_idx = true;
	txo->tx_idx = -1;
	return txo;
}

static void append_txo(char **s, const struct txo *txo)
{
	tal_append_fmt(s, "{ 'n': %d, 'value':%lld, 'address':%s, 'tx_idx':",
		       txo->n, (long long)txo->value,
		       txo->address ? txo->address : "None");
	if (txo->has_tx_idx)
		tal_append_fmt(s, "%lld }", (long long)txo->tx_idx);
	else
		tal_append_fmt(s, "None }");
}

static void append_txo_list(char **s, struct txo **list)
{
	size_t i;

	tal_append_fmt(s, "[");
	for (i = 0; i < tal_count(list); i++) {
		if (i > 0)
			tal_append_fmt(s, ", ");
		append_txo(s, list[i]);
	}
	tal_append_fmt(s, "]");
}

char *transaction_str(const tal_t *ctx, const struct transaction *tx)
{
	char *s = tal_fmt(ctx, "{ 'height': %lld, 'time':", (long long)tx->height);

	if (tx->has_time)
		tal_append_fmt(&s, "%lld", (long long)tx->time);
	else
		tal_append_fmt(&s, "None");
	tal_append_fmt(&s, ", 'txid':%s, 'inputs':", tx->txid);
	append_txo_list(&s, tx->inputs);
	tal_append_fmt(&s, ", 'outputs':");
	append_txo_list(&s, tx->outputs);
	tal_append_fmt(&s, " }");
	return s;
}
