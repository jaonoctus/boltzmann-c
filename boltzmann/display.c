#include "boltzmann/display.h"
#include "common/pyfmt.h"

#include <math.h>
#include <stdio.h>

/* Python's repr of an (address, amount) tuple. */
static char *labelled_txo_str(const tal_t *ctx, const struct labelled_txo *txo)
{
	if (txo->address)
		return tal_fmt(ctx, "('%s', %lld)", txo->address,
			       (long long)txo->value);
	return tal_fmt(ctx, "(None, %lld)", (long long)txo->value);
}

/* Python's repr of a list of (address, amount) tuples. */
static char *labelled_txos_str(const tal_t *ctx,
			       const struct labelled_txo *txos)
{
	char *s = tal_strdup(ctx, "[");
	size_t i;

	for (i = 0; i < tal_count(txos); i++)
		tal_append_fmt(&s, "%s%s", i > 0 ? ", " : "",
			       labelled_txo_str(ctx, &txos[i]));
	tal_append_fmt(&s, "]");
	return s;
}

static void print_matrix(const tal_t *ctx, const struct tx_analysis *an)
{
	size_t n = an->n_rows * an->n_cols, i;
	char *s;

	if (an->nb_cmbn != 0) {
		/* Each cell becomes the probability of that link. */
		double *probs = tal_arr(ctx, double, n);

		for (i = 0; i < n; i++)
			probs[i] = (double)an->matrix[i] / (double)an->nb_cmbn;
		printf("\nLinkability Matrix (probabilities) :\n");
		s = numpy_str_f64_matrix(ctx, probs, an->n_rows, an->n_cols);
	} else {
		/* Only the precheck ran: raw 0/1 deterministic-link flags. */
		s64 *counts = tal_arr(ctx, s64, n);

		for (i = 0; i < n; i++)
			counts[i] = (s64)an->matrix[i];
		printf("\nLinkability Matrix (#combinations with link) :\n");
		s = numpy_str_s64_matrix(ctx, counts, an->n_rows, an->n_cols);
	}
	printf("%s\n", s);
}

void display_results(const struct tx_analysis *an)
{
	tal_t *ctx = tal(NULL, char);
	size_t n_in = tal_count(an->inputs), n_out = tal_count(an->outputs);
	size_t i, j;

	printf("\nInputs = %s\n", labelled_txos_str(ctx, an->inputs));
	printf("\nOutputs = %s\n", labelled_txos_str(ctx, an->outputs));
	printf("\nFees = %lld satoshis\n", (long long)an->fees);

	if (an->intrafees.maker > 0 && an->intrafees.taker > 0) {
		printf("\nHypothesis: Max intrafees received by a participant = %lld satoshis\n",
		       (long long)an->intrafees.maker);
		printf("Hypothesis: Max intrafees paid by a participant = %lld satoshis\n",
		       (long long)an->intrafees.taker);
	}

	printf("\nNb combinations = %llu\n", (unsigned long long)an->nb_cmbn);
	if (an->nb_cmbn > 0)
		printf("Tx entropy = %f bits\n", log2((double)an->nb_cmbn));

	if (an->efficiency > 0)
		printf("Wallet efficiency = %f%% (%f bits)\n",
		       an->efficiency * 100, log2(an->efficiency));

	/* Printed with a percent sign by the reference implementation, but
	 * it is really bits per txo: there is no factor of 100.
	 */
	if (an->nb_cmbn > 0)
		printf("Entropy density = %f%%\n",
		       log2((double)an->nb_cmbn) / (double)(n_in + n_out));

	if (!an->matrix) {
		if (an->nb_cmbn == 0)
			printf("\nSkipped processing of this transaction (too many inputs and/or outputs)\n");
	} else {
		size_t dl_count = 0;

		print_matrix(ctx, an);

		/* A link present in every combination is certain. */
		printf("\nDeterministic links :\n");
		for (i = 0; i < n_out; i++) {
			for (j = 0; j < n_in; j++) {
				u64 cell = an->matrix[i * an->n_cols + j];

				if (cell != an->nb_cmbn || cell == 0)
					continue;
				printf("%s & %s are deterministically linked\n",
				       labelled_txo_str(ctx, &an->inputs[j]),
				       labelled_txo_str(ctx, &an->outputs[i]));
				dl_count++;
			}
		}
		printf("\nDeterministic link ratio = %f%%\n",
		       (double)dl_count / (double)(n_out * n_in) * 100);
	}
	tal_free(ctx);
}
