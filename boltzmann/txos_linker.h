#ifndef BOLTZMANN_TXOS_LINKER_H
#define BOLTZMANN_TXOS_LINKER_H
/*
 * The txos linker: the heart of Boltzmann.
 *
 * Given the inputs and outputs of a transaction, it counts every way the
 * inputs can be partitioned among the outputs so that the amounts still
 * balance (each "combination" is one consistent story of who paid whom),
 * and for every (input, output) pair it counts in how many of those
 * combinations the two belong to the same sub-transaction.
 *
 * Vocabulary, shared with the Python reference implementation:
 *
 *   txo         one input or output, identified by a short id such as
 *               "I2" or "O0" and its amount in satoshis.
 *   aggregate   a subset of inputs (or of outputs), encoded as a bitmask:
 *               bit k set means txo k (in value-sorted order) is in it.
 *   match       an input aggregate and an output aggregate whose sums are
 *               equal, up to the transaction fee.
 *   combination one complete partition of all inputs and all outputs
 *               into matching aggregate pairs.
 *   link        an (input, output) pair that share a sub-transaction in a
 *               given combination.
 *   pack        several txos known to belong to one entity, treated as a
 *               single txo during the search and expanded again at the end.
 */
#include "common/tal.h"
#include "common/types.h"

/**
 * struct txo_entry - a txo as the linker sees it: an id and an amount.
 */
struct txo_entry {
	const char *id;
	s64 value;
};

/**
 * struct id_set - a set of txo ids known to be controlled by one entity.
 */
struct id_set {
	const char **ids;	/* tal_arr */
};

/* Processing options, combined as a bitmask. */
enum linker_option {
	/* Look for deterministic links without computing the entropy
	 * (Coinjoin Sudoku).  Cheap; also used to shrink the main search.
	 */
	OPT_PRECHECK = 1 << 0,
	/* Compute the number of combinations and the linkability matrix. */
	OPT_LINKABILITY = 1 << 1,
	/* Treat the mining fee as one more output paid by a single entity. */
	OPT_MERGE_FEES = 1 << 2,
	/* Pack inputs sharing an address (handled by the caller). */
	OPT_MERGE_INPUTS = 1 << 3,
	/* Accepted for compatibility; has no effect, see tx_processor.c. */
	OPT_MERGE_OUTPUTS = 1 << 4,
};

/**
 * struct linker_result - what the linker computes.
 *
 * @matrix: linkability matrix, rows = outputs, columns = inputs, in the
 *          order of @outputs / @inputs; cell = number of combinations in
 *          which that input and output are linked.  NULL when it could
 *          not be computed (too many txos, or the time limit was hit).
 * @nb_cmbn: number of valid combinations; 0 when not computed.
 * @inputs / @outputs: the txos sorted by decreasing amount, packs expanded.
 */
struct linker_result {
	u64 *matrix;
	size_t n_rows, n_cols;
	u64 nb_cmbn;
	struct txo_entry *inputs;
	struct txo_entry *outputs;
};

/**
 * struct intrafees - tolerance for fees paid between coinjoin participants.
 *
 * In a JoinMarket-style coinjoin the taker pays the makers, so aggregates
 * need not balance exactly.  @maker is the most a participant may receive,
 * @taker the most a participant may pay.  Both zero means exact matching.
 */
struct intrafees {
	double maker;
	double taker;
};

/**
 * txos_linker_process - analyse one transaction.
 * @ctx: tal context for the result.
 * @inputs, @outputs: txos as (id, amount) entries, tal arrays.
 * @fees: mining fee in satoshis (sum of inputs minus sum of outputs).
 * @linked_txos: sets of input ids known to be controlled by one entity.
 * @options: bitmask of enum linker_option.
 * @intrafees: see struct intrafees.
 * @max_duration: give up on the combination search after this many seconds.
 * @max_txos: skip the search if more inputs or outputs than this.
 */
struct linker_result *txos_linker_process(const tal_t *ctx,
					  const struct txo_entry *inputs,
					  const struct txo_entry *outputs,
					  s64 fees,
					  const struct id_set *linked_txos,
					  u32 options,
					  struct intrafees intrafees,
					  double max_duration,
					  size_t max_txos);

/**
 * merge_sets - merge every pair of sets that share an element, repeatedly,
 * until all sets are disjoint.
 */
struct id_set *merge_sets(const tal_t *ctx, const struct id_set *sets);

/**
 * id_set_contains - membership test on an id set.
 */
bool id_set_contains(const struct id_set *set, const char *id);

#endif /* BOLTZMANN_TXOS_LINKER_H */
