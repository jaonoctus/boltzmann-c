#ifndef BOLTZMANN_TX_PROCESSOR_H
#define BOLTZMANN_TX_PROCESSOR_H
/*
 * The tx processor: everything around the linker.
 *
 * It turns a struct transaction into the (id, amount) entries the linker
 * works on, decides which inputs to merge, detects coinjoin patterns for
 * the intrafees heuristic, runs the linker, scores the result against a
 * perfect coinjoin and maps the ids back to addresses for display.
 */
#include "boltzmann/transaction.h"
#include "boltzmann/txos_linker.h"
#include "common/tal.h"
#include "common/types.h"

/**
 * struct labelled_txo - a txo ready for display: address and amount.
 * @address may be NULL when the provider had none (printed as None).
 */
struct labelled_txo {
	const char *address;
	s64 value;
};

/**
 * struct tx_analysis - the results for one transaction.
 *
 * @matrix: linkability counts, rows = @outputs, cols = @inputs; NULL when
 *          not computed.  Note that with OPT_MERGE_FEES the matrix has one
 *          more row than @outputs (the synthetic fee output has no address
 *          and is not displayed), exactly like the reference implementation.
 * @nb_cmbn: number of combinations (0 = not computed).
 * @fees: mining fee in satoshis.
 * @intrafees: the coinjoin intrafees hypothesis used, zero if none.
 * @efficiency: nb_cmbn / nb_cmbn(closest perfect coinjoin); 0 when
 *          nb_cmbn <= 1.
 * @duration: processing time in seconds.
 */
struct tx_analysis {
	u64 *matrix;
	size_t n_rows, n_cols;
	u64 nb_cmbn;
	struct labelled_txo *inputs;
	struct labelled_txo *outputs;
	s64 fees;
	struct intrafees intrafees;
	double efficiency;
	double duration;
};

/**
 * process_tx - analyse a transaction.
 * @options: bitmask of enum linker_option.
 * @max_duration: seconds allowed for the combination search.
 * @max_txos: skip the search beyond this many inputs or outputs.
 * @max_cj_intrafees_ratio: max intrafees paid by the taker of a coinjoin,
 *          as a fraction of the coinjoined amount (0 disables the heuristic).
 */
struct tx_analysis *process_tx(const tal_t *ctx, const struct transaction *tx,
			       u32 options, double max_duration, size_t max_txos,
			       double max_cj_intrafees_ratio);

/**
 * check_coinjoin_pattern - does the output side look like a coinjoin?
 *
 * Looks for n > 1 outputs of the same amount with at most 2 outputs per
 * participant.  Returns true and fills @nb_ptcpts / @cj_amount if so.
 */
bool check_coinjoin_pattern(const struct txo_entry *outputs,
			    size_t max_nb_entities,
			    size_t *nb_ptcpts, s64 *cj_amount);

/**
 * compute_wallet_efficiency - nb_cmbn relative to the closest perfect
 * coinjoin of the same shape.  0 when nb_cmbn is 1.
 */
double compute_wallet_efficiency(size_t n_in, size_t n_out, u64 nb_cmbn);

#endif /* BOLTZMANN_TX_PROCESSOR_H */
