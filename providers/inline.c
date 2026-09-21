/*
 * A made-up transaction given on the command line, for trying shapes
 * without fetching anything:
 *
 *	ludwig --inputs=2,3 --outputs=4,1
 *
 * Each side is a comma-separated list of amounts in satoshis, each
 * optionally prefixed with a label: "a:2,b:3".  Unlabelled inputs are
 * called a, b, c... and outputs A, B, C..., as in the lesson; repeating a
 * label stands for address reuse.  The fee is whatever the inputs exceed
 * the outputs by.
 */
#include "common/utils.h"
#include "providers/provider.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *default_label(const tal_t *ctx, bool is_input, int n)
{
	if (n < 26)
		return tal_fmt(ctx, "%c", (is_input ? 'a' : 'A') + n);
	return tal_fmt(ctx, is_input ? "in%d" : "out%d", n + 1);
}

static bool parse_txos(const tal_t *ctx, struct txo ***list, const char *spec,
		       bool is_input, char **err)
{
	const char *flag = is_input ? "--inputs" : "--outputs";
	char *copy = tal_strdup(ctx, spec), *tok, *save = NULL;
	int n = 0;

	for (tok = strtok_r(copy, ",", &save); tok;
	     tok = strtok_r(NULL, ",", &save)) {
		char *colon, *end;
		const char *label = NULL;
		long long value;

		while (*tok == ' ')
			tok++;
		colon = strchr(tok, ':');
		if (colon) {
			*colon = '\0';
			label = tok;
			tok = colon + 1;
			if (*label == '\0') {
				*err = tal_fmt(ctx, "%s: empty label before \":%s\"",
					       flag, tok);
				return false;
			}
		}
		errno = 0;
		value = strtoll(tok, &end, 10);
		if (*tok == '\0' || *end != '\0' || value < 0 || errno) {
			*err = tal_fmt(ctx, "%s: bad amount \"%s\" (expected a non-negative integer, optionally LABEL:AMOUNT)",
				       flag, tok);
			return false;
		}
		if (!label)
			label = default_label(copy, is_input, n);
		txo_new(list, n, value, label);
		n++;
	}
	if (n == 0) {
		*err = tal_fmt(ctx, "%s: no amounts given", flag);
		return false;
	}
	tal_free(copy);
	return true;
}

static struct transaction *inline_get_tx(const tal_t *ctx,
					 const struct blockchain_provider *provider,
					 const char *txid, bool mainnet,
					 char **err)
{
	struct transaction *tx = transaction_new(ctx, "inline");
	s64 sum_in = 0, sum_out = 0;
	size_t i;

	*err = NULL;
	if (!parse_txos(ctx, &tx->inputs, provider->inputs, true, err) ||
	    !parse_txos(ctx, &tx->outputs, provider->outputs, false, err))
		return tal_free(tx);

	for (i = 0; i < tal_count(tx->inputs); i++)
		sum_in += tx->inputs[i]->value;
	for (i = 0; i < tal_count(tx->outputs); i++)
		sum_out += tx->outputs[i]->value;
	if (sum_out > sum_in) {
		*err = tal_fmt(ctx, "outputs (%lld) exceed inputs (%lld): the fee would be negative",
			       (long long)sum_out, (long long)sum_in);
		return tal_free(tx);
	}
	return tx;
}

struct blockchain_provider *inline_provider(const tal_t *ctx,
					    const char *inputs,
					    const char *outputs)
{
	struct blockchain_provider *p = talz(ctx, struct blockchain_provider);

	p->description = "inline transaction";
	p->get_tx = inline_get_tx;
	p->inputs = tal_strdup(p, inputs);
	p->outputs = tal_strdup(p, outputs);
	return p;
}
