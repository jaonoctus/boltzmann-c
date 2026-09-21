/*
 * The txos linker.  See txos_linker.h for the vocabulary.
 *
 * The file is laid out in the order the analysis runs:
 *
 *   1. id sets and packing           (which txos belong together)
 *   2. preparation                   (sort txos, sum every aggregate)
 *   3. matching aggregates by value  (which input subsets can pay which
 *                                     output subsets)
 *   4. the precheck                  (deterministic links, cheap)
 *   5. the combination search        (count every consistent partition
 *                                     and how often each link occurs)
 *   6. unpacking and the entry point
 */
#include "boltzmann/txos_linker.h"
#include "common/u64map.h"
#include "common/utils.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Labels for synthetic txos. */
#define FEES_ID "FEES"
#define PACK_PREFIX "PACK"

/**
 * struct pack - several inputs collapsed into one synthetic input.
 * @entry: the synthetic txo (label + summed amount) as it appears in the
 *         input list during the search.
 * @members: the real txos it replaced, in their original relative order.
 */
struct pack {
	struct txo_entry entry;
	struct txo_entry *members;
};

/**
 * struct agg_match - the output aggregates that balance one input value.
 *
 * All input aggregates summing to @in_val can be paid to any of @out_aggs
 * (their sums differ from @in_val by at most the fee tolerance).
 */
struct agg_match {
	s64 in_val;
	u32 *out_aggs;		/* sorted ascending, unique */
};

/**
 * struct agg_split - one way to cut an input aggregate in two.
 *
 * @big and @small are disjoint, both are "matched" aggregates and
 * @big > @small, so each unordered split is listed exactly once.
 */
struct agg_split {
	u32 big;
	u32 small;
};

/**
 * struct txos_linker - all the state of one analysis.
 */
struct txos_linker {
	/* What we were asked to analyse. */
	struct txo_entry *inputs;	/* working copy, mutated by packing */
	struct txo_entry *outputs;
	s64 orig_fees;
	s64 fees;			/* 0 when fees became an output */
	struct intrafees intrafees;
	bool has_intrafees;
	u32 options;
	double max_duration;
	size_t max_txos;

	/* Packs created so far, in creation order. */
	struct pack *packs;

	/* Sum of each aggregate, indexed by bitmask (2^n entries). */
	s64 *in_agg_vals;
	s64 *out_agg_vals;

	/* Matching, see match_aggregates_by_value(). */
	struct agg_match *matches;	/* by increasing in_val */
	s32 *match_of_in_agg;		/* index into matches, -1 if none */
	u32 *matched_in_aggs;		/* every matched input agg, ascending */

	/* Every valid split of every input aggregate, see
	 * compute_input_splits().  Indexed by the aggregate being split.
	 */
	struct agg_split **splits_of;
};

/*
 * 1. Id sets and packing
 */

/* Entry arrays are rebuilt often (packing, sorting); the id strings are
 * owned by @ctx so they outlive any particular array.
 */
static struct txo_entry *copy_entries(const tal_t *ctx,
				      const struct txo_entry *entries)
{
	struct txo_entry *copy = tal_arr(ctx, struct txo_entry, tal_count(entries));
	size_t i;

	for (i = 0; i < tal_count(entries); i++) {
		copy[i].id = tal_strdup(ctx, entries[i].id);
		copy[i].value = entries[i].value;
	}
	return copy;
}

bool id_set_contains(const struct id_set *set, const char *id)
{
	size_t i;

	for (i = 0; i < tal_count(set->ids); i++) {
		if (streq(set->ids[i], id))
			return true;
	}
	return false;
}

static bool id_sets_disjoint(const struct id_set *a, const struct id_set *b)
{
	size_t i;

	for (i = 0; i < tal_count(a->ids); i++) {
		if (id_set_contains(b, a->ids[i]))
			return false;
	}
	return true;
}

static void id_set_union(struct id_set *into, const struct id_set *from)
{
	size_t i;

	for (i = 0; i < tal_count(from->ids); i++) {
		if (!id_set_contains(into, from->ids[i]))
			tal_arr_expand(&into->ids, from->ids[i]);
	}
}

static struct id_set id_set_copy(const tal_t *ctx, const struct id_set *set)
{
	struct id_set copy;
	size_t i;

	copy.ids = tal_arr(ctx, const char *, tal_count(set->ids));
	for (i = 0; i < tal_count(set->ids); i++)
		copy.ids[i] = set->ids[i];
	return copy;
}

struct id_set *merge_sets(const tal_t *ctx, const struct id_set *sets)
{
	struct id_set *pending, *merged_sets;
	bool merged = true;
	size_t i;

	pending = tal_arr(ctx, struct id_set, tal_count(sets));
	for (i = 0; i < tal_count(sets); i++)
		pending[i] = id_set_copy(pending, &sets[i]);

	/* Repeatedly take the first pending set, absorb every other pending
	 * set that overlaps it, and start over until nothing merges.
	 */
	while (merged) {
		merged = false;
		merged_sets = tal_arr(ctx, struct id_set, 0);
		while (tal_count(pending) > 0) {
			struct id_set current = pending[0];
			struct id_set *rest = tal_arr(ctx, struct id_set, 0);

			for (i = 1; i < tal_count(pending); i++) {
				if (id_sets_disjoint(&pending[i], &current)) {
					tal_arr_expand(&rest, pending[i]);
				} else {
					merged = true;
					id_set_union(&current, &pending[i]);
				}
			}
			tal_arr_expand(&merged_sets, current);
			pending = rest;
		}
		pending = merged_sets;
	}
	return pending;
}

/**
 * pack_linked_txos - collapse each set of linked inputs into one txo.
 *
 * Only inputs are ever packed: the reference implementation looks for the
 * ids in the input list, so sets of output ids fall through unchanged.
 * A pack's amount is the sum of its members.  It is appended at the end of
 * the input list; the next preparation step re-sorts everything anyway.
 */
static void pack_linked_txos(struct txos_linker *linker,
			     const struct id_set *linked_txos)
{
	struct id_set *sets = merge_sets(linker, linked_txos);
	size_t s, i;

	for (s = 0; s < tal_count(sets); s++) {
		struct pack pack;
		struct txo_entry *remaining;
		s64 total = 0;

		pack.members = tal_arr(linker->packs, struct txo_entry, 0);
		remaining = tal_arr(linker, struct txo_entry, 0);
		for (i = 0; i < tal_count(linker->inputs); i++) {
			struct txo_entry *in = &linker->inputs[i];

			if (id_set_contains(&sets[s], in->id)) {
				tal_arr_expand(&pack.members, *in);
				total += in->value;
			} else {
				tal_arr_expand(&remaining, *in);
			}
		}
		if (tal_count(pack.members) == 0) {
			tal_free(pack.members);
			tal_free(remaining);
			continue;
		}

		/* Pack labels are numbered from 1 across the whole analysis. */
		pack.entry.id = tal_fmt(linker->packs, PACK_PREFIX "_I%zu",
					tal_count(linker->packs) + 1);
		pack.entry.value = total;
		tal_arr_expand(&remaining, pack.entry);
		tal_free(linker->inputs);
		linker->inputs = remaining;
		tal_arr_expand(&linker->packs, pack);
	}
	tal_free(sets);
}

/**
 * unpack_matrix_and_txos - undo the packing in the results.
 *
 * Each pack is replaced by its members at the same position in the txo
 * list, and its matrix column is duplicated once per member: everything
 * that is true of the pack is true of each member.  Packs are undone in
 * reverse creation order because a later pack may contain an earlier one.
 */
static void unpack_matrix_and_txos(struct txos_linker *linker,
				   struct linker_result *res)
{
	size_t p;

	for (p = tal_count(linker->packs); p-- > 0;) {
		const struct pack *pack = &linker->packs[p];
		size_t n_members = tal_count(pack->members);
		size_t n_in = tal_count(res->inputs);
		size_t idx, i, r, c;
		struct txo_entry *inputs;

		for (idx = 0; idx < n_in; idx++) {
			if (streq(res->inputs[idx].id, pack->entry.id) &&
			    res->inputs[idx].value == pack->entry.value)
				break;
		}
		assert(idx < n_in);

		if (res->matrix) {
			size_t new_cols = res->n_cols - 1 + n_members;
			u64 *m = tal_arr(res, u64, res->n_rows * new_cols);

			for (r = 0; r < res->n_rows; r++) {
				const u64 *old_row = &res->matrix[r * res->n_cols];
				u64 *new_row = &m[r * new_cols];

				for (c = 0; c < idx; c++)
					new_row[c] = old_row[c];
				for (i = 0; i < n_members; i++)
					new_row[idx + i] = old_row[idx];
				for (c = idx + 1; c < res->n_cols; c++)
					new_row[c - 1 + n_members] = old_row[c];
			}
			tal_free(res->matrix);
			res->matrix = m;
			res->n_cols = new_cols;
		}

		inputs = tal_arr(res, struct txo_entry, 0);
		for (c = 0; c < idx; c++)
			tal_arr_expand(&inputs, res->inputs[c]);
		for (i = 0; i < n_members; i++) {
			struct txo_entry member = pack->members[i];

			member.id = tal_strdup(res, member.id);
			tal_arr_expand(&inputs, member);
		}
		for (c = idx + 1; c < n_in; c++)
			tal_arr_expand(&inputs, res->inputs[c]);
		tal_free(res->inputs);
		res->inputs = inputs;
	}
}

/*
 * 2. Preparation
 */

static int cmp_entry_value_desc(const void *a, const void *b)
{
	const struct txo_entry *ea = a, *eb = b;

	if (ea->value > eb->value)
		return -1;
	if (ea->value < eb->value)
		return 1;
	return 0;
}

/**
 * aggregate_values - the sum of every subset of @entries.
 *
 * Returns a tal array of 2^n sums, indexed by bitmask.  Built incrementally:
 * the sum of mask m is the sum of m without its lowest bit, plus that txo.
 */
static s64 *aggregate_values(const tal_t *ctx, const struct txo_entry *entries)
{
	size_t n = tal_count(entries);
	size_t n_aggs = (size_t)1 << n;
	s64 *vals = tal_arr(ctx, s64, n_aggs);
	u32 mask;

	vals[0] = 0;
	for (mask = 1; mask < n_aggs; mask++) {
		u32 lowest_bit = __builtin_ctz(mask);

		vals[mask] = vals[mask & (mask - 1)] + entries[lowest_bit].value;
	}
	return vals;
}

/**
 * prepare_txos - drop empty txos, sort by decreasing amount, sum aggregates.
 */
static void prepare_txos(struct txos_linker *linker, struct txo_entry **entries,
			 s64 **agg_vals)
{
	struct txo_entry *kept = tal_arr(linker, struct txo_entry, 0);
	size_t i;

	for (i = 0; i < tal_count(*entries); i++) {
		if ((*entries)[i].value > 0)
			tal_arr_expand(&kept, (*entries)[i]);
	}
	stable_sort(kept, tal_count(kept), sizeof(*kept), cmp_entry_value_desc);
	tal_free(*entries);
	*entries = kept;

	tal_free(*agg_vals);
	*agg_vals = aggregate_values(linker, kept);
}

static void prepare_data(struct txos_linker *linker)
{
	prepare_txos(linker, &linker->inputs, &linker->in_agg_vals);
	prepare_txos(linker, &linker->outputs, &linker->out_agg_vals);
}

/*
 * 3. Matching aggregates by value
 */

/**
 * struct value_group - every aggregate (bitmask) that sums to one value.
 */
struct value_group {
	s64 value;
	u32 *aggs;
};

/* An aggregate paired with its sum, so plain qsort() can order them. */
struct val_agg {
	s64 value;
	u32 agg;
};

static int cmp_val_agg(const void *a, const void *b)
{
	const struct val_agg *va = a, *vb = b;

	if (va->value != vb->value)
		return va->value < vb->value ? -1 : 1;
	/* Same value: keep ascending mask order. */
	return va->agg < vb->agg ? -1 : (va->agg > vb->agg ? 1 : 0);
}

/**
 * group_by_value - group aggregates by their sum, groups sorted by value.
 *
 * This replaces the numpy idiom of np.unique() followed by np.where():
 * each aggregate is visited once.
 */
static struct value_group *group_by_value(const tal_t *ctx, const s64 *agg_vals)
{
	size_t n_aggs = tal_count(agg_vals);
	struct val_agg *sorted = tal_arr(ctx, struct val_agg, n_aggs);
	struct value_group *groups = tal_arr(ctx, struct value_group, 0);
	size_t i;

	for (i = 0; i < n_aggs; i++) {
		sorted[i].value = agg_vals[i];
		sorted[i].agg = (u32)i;
	}
	qsort(sorted, n_aggs, sizeof(*sorted), cmp_val_agg);

	for (i = 0; i < n_aggs; i++) {
		if (tal_count(groups) == 0 ||
		    groups[tal_count(groups) - 1].value != sorted[i].value) {
			struct value_group g;

			g.value = sorted[i].value;
			g.aggs = tal_arr(groups, u32, 0);
			tal_arr_expand(&groups, g);
		}
		tal_arr_expand(&groups[tal_count(groups) - 1].aggs, sorted[i].agg);
	}
	tal_free(sorted);
	return groups;
}

static int cmp_u32(const void *a, const void *b)
{
	u32 ua = *(const u32 *)a, ub = *(const u32 *)b;

	return ua < ub ? -1 : (ua > ub ? 1 : 0);
}

/**
 * agg_match_contains - can this input value be paid to output agg @out_agg?
 */
static bool agg_match_contains(const struct agg_match *m, u32 out_agg)
{
	return bsearch(&out_agg, m->out_aggs, tal_count(m->out_aggs),
		       sizeof(u32), cmp_u32) != NULL;
}

/**
 * values_can_match - can an input aggregate summing to @in_val pay an
 * output aggregate summing to @out_val?
 *
 * Without intrafees the inputs must cover the outputs and the difference
 * (what is left for the miner) must not exceed the fee actually paid.
 * With intrafees, participants may also pay or receive from each other.
 */
static bool values_can_match(const struct txos_linker *linker, s64 in_val,
			     s64 out_val)
{
	s64 diff = in_val - out_val;

	if (!linker->has_intrafees)
		return diff >= 0 && diff <= linker->fees;

	/* fees_taker: tx fee plus what the taker hands to the makers.
	 * -fees_maker: what a maker may receive (ignores the tx fee).
	 */
	return (diff <= 0 && (double)diff >= -linker->intrafees.maker) ||
	       (diff >= 0 &&
		   (double)diff <= (double)linker->fees + linker->intrafees.taker);
}

/**
 * match_aggregates_by_value - find every (input aggregate, output aggregate)
 * pair whose sums balance.
 *
 * Fills linker->matches, match_of_in_agg and matched_in_aggs.
 */
static void match_aggregates_by_value(struct txos_linker *linker)
{
	struct value_group *in_groups = group_by_value(linker, linker->in_agg_vals);
	struct value_group *out_groups = group_by_value(linker, linker->out_agg_vals);
	size_t n_in_aggs = tal_count(linker->in_agg_vals);
	size_t gi, go, i;

	tal_free(linker->matches);
	tal_free(linker->match_of_in_agg);
	tal_free(linker->matched_in_aggs);
	linker->matches = tal_arr(linker, struct agg_match, 0);
	linker->match_of_in_agg = tal_arr(linker, s32, n_in_aggs);
	linker->matched_in_aggs = tal_arr(linker, u32, 0);
	for (i = 0; i < n_in_aggs; i++)
		linker->match_of_in_agg[i] = -1;

	for (gi = 0; gi < tal_count(in_groups); gi++) {
		const struct value_group *ing = &in_groups[gi];
		struct agg_match *m = NULL;

		for (go = 0; go < tal_count(out_groups); go++) {
			const struct value_group *outg = &out_groups[go];

			/* Output values are ascending: once they exceed the
			 * input value nothing further can balance.
			 */
			if (!linker->has_intrafees && outg->value > ing->value)
				break;
			if (!values_can_match(linker, ing->value, outg->value))
				continue;

			if (!m) {
				struct agg_match newm;

				newm.in_val = ing->value;
				newm.out_aggs = tal_arr(linker->matches, u32, 0);
				tal_arr_expand(&linker->matches, newm);
				m = &linker->matches[tal_count(linker->matches) - 1];
			}
			for (i = 0; i < tal_count(outg->aggs); i++)
				tal_arr_expand(&m->out_aggs, outg->aggs[i]);
		}

		if (m) {
			s32 idx = (s32)(tal_count(linker->matches) - 1);

			qsort(m->out_aggs, tal_count(m->out_aggs), sizeof(u32),
			      cmp_u32);
			for (i = 0; i < tal_count(ing->aggs); i++) {
				linker->match_of_in_agg[ing->aggs[i]] = idx;
				tal_arr_expand(&linker->matched_in_aggs,
					       ing->aggs[i]);
			}
		}
	}
	qsort(linker->matched_in_aggs, tal_count(linker->matched_in_aggs),
	      sizeof(u32), cmp_u32);
	tal_free(in_groups);
	tal_free(out_groups);
}

static const struct agg_match *match_for(const struct txos_linker *linker,
					 u32 in_agg)
{
	s32 idx = linker->match_of_in_agg[in_agg];

	assert(idx >= 0);
	return &linker->matches[idx];
}

/*
 * 4. The precheck: deterministic links
 */

/**
 * add_link_counts - for every input in @in_agg and output in @out_agg, add
 * @count to the matrix cell.  This is the outer product of the two masks.
 */
static void add_link_counts(u64 *matrix, size_t n_cols, u32 in_agg, u32 out_agg,
			    u64 count)
{
	u32 outs = out_agg;

	while (outs) {
		u32 o = __builtin_ctz(outs);
		u32 ins = in_agg;

		while (ins) {
			u32 i = __builtin_ctz(ins);

			matrix[o * n_cols + i] += count;
			ins &= ins - 1;
		}
		outs &= outs - 1;
	}
}

/**
 * struct link_coord - one deterministic link, as matrix coordinates.
 */
struct link_coord {
	size_t row;	/* output index */
	size_t col;	/* input index */
};

/**
 * check_deterministic_links - Coinjoin Sudoku.
 *
 * Counts, for every (input, output) pair, how many matching aggregate
 * pairs contain both.  An input takes part in some number of matching
 * pairs (the same number for every input, by symmetry: we read it off
 * input 0).  A pair (input, output) that appears in all of them is linked
 * whatever the true story is: a deterministic link.
 */
static struct link_coord *check_deterministic_links(struct txos_linker *linker)
{
	size_t n_in = tal_count(linker->inputs), n_out = tal_count(linker->outputs);
	u64 *counts = tal_arrz(linker, u64, n_in * n_out);
	u64 in0_count = 0;
	struct link_coord *links = tal_arr(linker, struct link_coord, 0);
	size_t a, b, r, c;

	for (a = 0; a < tal_count(linker->matched_in_aggs); a++) {
		u32 in_agg = linker->matched_in_aggs[a];
		const struct agg_match *m = match_for(linker, in_agg);

		for (b = 0; b < tal_count(m->out_aggs); b++) {
			add_link_counts(counts, n_in, in_agg, m->out_aggs[b], 1);
			if (in_agg & 1)
				in0_count++;
		}
	}

	for (r = 0; r < n_out; r++) {
		for (c = 0; c < n_in; c++) {
			if (counts[r * n_in + c] == in0_count) {
				struct link_coord lc = { r, c };

				tal_arr_expand(&links, lc);
			}
		}
	}
	tal_free(counts);
	return links;
}

/*
 * 5. The combination search
 */

/**
 * compute_input_splits - list every way to cut a matched input aggregate
 * into two disjoint matched aggregates.
 *
 * The search below decomposes the full input set by repeatedly splitting
 * off one aggregate; this table answers "how can @agg be split?" in O(1).
 * For a given parent the splits are generated with @big ascending, hence
 * @small descending, which the search relies on to stop early.
 */
static void compute_input_splits(struct txos_linker *linker)
{
	size_t n_matched = tal_count(linker->matched_in_aggs);
	size_t n_in_aggs = tal_count(linker->in_agg_vals);
	bool *usable = tal_arrz(linker, bool, n_in_aggs);
	u32 target, i, j;

	tal_free(linker->splits_of);
	linker->splits_of = tal_arrz(linker, struct agg_split *, n_in_aggs);
	if (n_matched < 2)
		return;

	/* The empty aggregate and the full set are not parts of a split. */
	target = linker->matched_in_aggs[n_matched - 1];
	for (i = 1; i + 1 < n_matched; i++)
		usable[linker->matched_in_aggs[i]] = true;

	for (i = 0; i <= target; i++) {
		u32 j_max;

		if (!usable[i])
			continue;
		j_max = i < target - i + 1 ? i : target - i + 1;
		for (j = 0; j < j_max; j++) {
			struct agg_split split = { i, j };

			if ((i & j) != 0 || !usable[j])
				continue;
			if (!linker->splits_of[i + j])
				linker->splits_of[i + j] =
					tal_arr(linker->splits_of, struct agg_split, 0);
			tal_arr_expand(&linker->splits_of[i + j], split);
		}
	}
	tal_free(usable);
}

/**
 * struct out_cmbn - one output-side decomposition step, matching an input
 * split: the output aggregate @left pays for the input part split off,
 * the rest is owed by the remaining input aggregate.
 *
 * @nb_parents: how many ways the decomposition could have arrived here.
 * @nb_children: how many complete combinations descend from here.
 */
struct out_cmbn {
	u32 left;
	u64 nb_parents;
	u64 nb_children;
};

/**
 * struct out_group - all out_cmbn entries sharing the same remaining
 * output aggregate @right.
 */
struct out_group {
	u32 right;
	struct out_cmbn *cmbns;
};

/**
 * struct out_cmbns - the output decompositions matching one input
 * decomposition (the Python d_out dictionary: {right: {left: (p, c)}}).
 */
struct out_cmbns {
	struct out_group *groups;
	s32 *group_of;		/* index into groups by right mask, -1 if none */
};

static struct out_cmbns *out_cmbns_new(const tal_t *ctx, size_t n_out_aggs)
{
	struct out_cmbns *oc = tal(ctx, struct out_cmbns);
	size_t i;

	oc->groups = tal_arr(oc, struct out_group, 0);
	oc->group_of = tal_arr(oc, s32, n_out_aggs);
	for (i = 0; i < n_out_aggs; i++)
		oc->group_of[i] = -1;
	return oc;
}

static struct out_group *out_cmbns_group(struct out_cmbns *oc, u32 right)
{
	s32 idx = oc->group_of[right];

	return idx < 0 ? NULL : &oc->groups[idx];
}

static void out_cmbns_add(struct out_cmbns *oc, u32 right, u32 left,
			  u64 nb_parents)
{
	struct out_group *g = out_cmbns_group(oc, right);
	struct out_cmbn c = { left, nb_parents, 0 };

	if (!g) {
		struct out_group newg;

		newg.right = right;
		newg.cmbns = tal_arr(oc->groups, struct out_cmbn, 0);
		oc->group_of[right] = (s32)tal_count(oc->groups);
		tal_arr_expand(&oc->groups, newg);
		g = &oc->groups[tal_count(oc->groups) - 1];
	}
	/* A (right, left) pair is reached from exactly one parent group,
	 * so this never overwrites an existing entry.
	 */
	tal_arr_expand(&g->cmbns, c);
}

/**
 * struct search_task - one frame of the depth-first search.
 *
 * The input side has been decomposed into aggregates split off so far;
 * @left_in is the most recent one and @right_in is what remains to be
 * decomposed.  @outs holds every output-side decomposition consistent
 * with it.  @next_split remembers where to resume in splits_of[right_in]
 * when this frame becomes the top of the stack again.
 */
struct search_task {
	size_t next_split;
	u32 left_in;
	u32 right_in;
	struct out_cmbns *outs;
};

/**
 * expand_task - given the current frame and one more input split
 * (@small paid separately, @big remaining), compute the output-side
 * decompositions consistent with it.
 *
 * For every output decomposition of the parent, and every output
 * aggregate that can be paid by @small and is disjoint from what the
 * parent already assigned, check that the remainder can be paid by @big.
 */
static struct out_cmbns *expand_task(struct txos_linker *linker,
				     const struct search_task *task,
				     u32 small, u32 big, u32 out_target)
{
	size_t n_out_aggs = tal_count(linker->out_agg_vals);
	struct out_cmbns *child = out_cmbns_new(linker, n_out_aggs);
	const struct agg_match *small_match = match_for(linker, small);
	const struct agg_match *big_match = match_for(linker, big);
	size_t g, c, k;

	for (g = 0; g < tal_count(task->outs->groups); g++) {
		const struct out_group *group = &task->outs->groups[g];
		u32 assigned = out_target - group->right;
		u64 nb_parents = 0;

		for (c = 0; c < tal_count(group->cmbns); c++)
			nb_parents += group->cmbns[c].nb_parents;

		for (k = 0; k < tal_count(small_match->out_aggs); k++) {
			u32 left = small_match->out_aggs[k];
			u32 new_assigned, right;

			if (assigned & left)
				continue;
			new_assigned = assigned + left;
			right = out_target - new_assigned;
			if ((new_assigned & right) == 0 &&
			    agg_match_contains(big_match, right))
				out_cmbns_add(child, right, left, nb_parents);
		}
	}
	return child;
}

/**
 * finish_task - a frame is exhausted: fold its results into the parent
 * and into the link counts.
 *
 * Each of the frame's output decompositions describes a link between
 * (@right_in, right) and between (@left_in, left).  It happened
 * nb_parents ways above it and leads to nb_children + 1 complete
 * combinations below it (the +1 is the decomposition stopping here).
 */
static void finish_task(struct search_task *task, struct search_task *parent,
			struct u64map *link_counts)
{
	size_t g, c, k;

	for (g = 0; g < tal_count(task->outs->groups); g++) {
		const struct out_group *group = &task->outs->groups[g];

		for (c = 0; c < tal_count(group->cmbns); c++) {
			const struct out_cmbn *cmbn = &group->cmbns[c];
			u64 nb_occur = cmbn->nb_children + 1;
			struct out_group *pgroup;

			u64map_add(link_counts,
				   pack_pair(task->right_in, group->right),
				   cmbn->nb_parents);
			u64map_add(link_counts,
				   pack_pair(task->left_in, cmbn->left),
				   cmbn->nb_parents * nb_occur);

			/* Back-propagate: the parent's entry for the output
			 * aggregate we split (left + right) gained children.
			 */
			pgroup = out_cmbns_group(parent->outs, cmbn->left + group->right);
			if (!pgroup)
				continue;
			for (k = 0; k < tal_count(pgroup->cmbns); k++)
				pgroup->cmbns[k].nb_children += nb_occur;
		}
	}
}

/**
 * compute_link_matrix - count combinations and fill the matrix.
 *
 * Depth-first search over the ways to decompose the full input set.  Each
 * frame splits off one more input aggregate (in increasing order, so each
 * partition is visited once) and keeps only the output decompositions
 * that stay consistent.  Link counts are accumulated as frames finish.
 *
 * Returns false if the time limit was hit.
 */
static bool compute_link_matrix(struct txos_linker *linker, u64 *nb_cmbn,
				u64 **matrix)
{
	size_t n_in = tal_count(linker->inputs), n_out = tal_count(linker->outputs);
	u32 in_target = ((u32)1 << n_in) - 1;
	u32 out_target = ((u32)1 << n_out) - 1;
	struct u64map *link_counts = u64map_new(linker);
	struct search_task *stack = tal_arr(linker, struct search_task, 0);
	struct search_task root;
	double start = time_now_seconds();
	const struct u64map_entry *e;
	u64 *m;

	/* The root frame: nothing split off yet, every input remaining,
	 * and the single output decomposition "everything still owed".
	 */
	root.next_split = 0;
	root.left_in = 0;
	root.right_in = in_target;
	root.outs = out_cmbns_new(linker, tal_count(linker->out_agg_vals));
	out_cmbns_add(root.outs, out_target, 0, 1);
	tal_arr_expand(&stack, root);
	*nb_cmbn = 0;

	while (tal_count(stack) > 0) {
		struct search_task *task = &stack[tal_count(stack) - 1];
		const struct agg_split *splits = linker->splits_of[task->right_in];
		size_t n_splits = tal_count(splits);
		bool pushed = false;

		if (time_now_seconds() - start >= linker->max_duration)
			return false;

		while (task->next_split < n_splits) {
			const struct agg_split *split = &splits[task->next_split];
			struct search_task child;

			/* Splits are ordered by decreasing small part; once
			 * it is not above the part split off before, the
			 * remaining splits would revisit known partitions.
			 */
			if (split->small <= task->left_in) {
				task->next_split = n_splits;
				break;
			}
			child.next_split = 0;
			child.left_in = split->small;
			child.right_in = split->big;
			child.outs = expand_task(linker, task, split->small,
						 split->big, out_target);
			task->next_split++;
			tal_arr_expand(&stack, child);
			pushed = true;
			break;
		}
		if (pushed)
			continue;

		/* Frame exhausted: pop it. */
		{
			struct search_task done = stack[tal_count(stack) - 1];

			tal_resize(&stack, tal_count(stack) - 1);
			if (tal_count(stack) == 0) {
				const struct out_group *g =
					out_cmbns_group(done.outs, out_target);

				assert(g && tal_count(g->cmbns) == 1 &&
				       g->cmbns[0].left == 0);
				*nb_cmbn = g->cmbns[0].nb_children;
			} else {
				finish_task(&done, &stack[tal_count(stack) - 1],
					    link_counts);
			}
			tal_free(done.outs);
		}
	}

	/* The trivial combination (all inputs pay all outputs together)
	 * links everything with everything, once.
	 */
	*nb_cmbn += 1;
	m = tal_arrz(linker, u64, n_in * n_out);
	add_link_counts(m, n_in, in_target, out_target, 1);
	u64map_foreach(link_counts, e)
		add_link_counts(m, n_in, pair_hi(e->key), pair_lo(e->key), e->value);
	*matrix = m;
	tal_free(link_counts);
	tal_free(stack);
	return true;
}

/*
 * 6. Entry point
 */

static bool within_limits(const struct txos_linker *linker)
{
	size_t n_in = tal_count(linker->inputs), n_out = tal_count(linker->outputs);

	return (n_in > n_out ? n_in : n_out) <= linker->max_txos;
}

struct linker_result *txos_linker_process(const tal_t *ctx,
					  const struct txo_entry *inputs,
					  const struct txo_entry *outputs,
					  s64 fees,
					  const struct id_set *linked_txos,
					  u32 options,
					  struct intrafees intrafees,
					  double max_duration,
					  size_t max_txos)
{
	struct txos_linker *linker = talz(ctx, struct txos_linker);
	struct linker_result *res = talz(ctx, struct linker_result);
	struct link_coord *dtrm_links = NULL;
	struct id_set *dtrm_sets = NULL;
	size_t n_in, n_out, i;

	linker->inputs = copy_entries(linker, inputs);
	linker->outputs = copy_entries(linker, outputs);
	linker->orig_fees = fees;
	linker->intrafees = intrafees;
	linker->has_intrafees = intrafees.maker != 0 || intrafees.taker != 0;
	linker->options = options;
	linker->max_duration = max_duration;
	linker->max_txos = max_txos;
	linker->packs = tal_arr(linker, struct pack, 0);

	/* Txos known to belong together are searched as one. */
	if (linked_txos && tal_count(linked_txos) > 0)
		pack_linked_txos(linker, linked_txos);

	/* Optionally treat the fee as an output paid by one participant. */
	if ((options & OPT_MERGE_FEES) && fees > 0) {
		struct txo_entry fee_out = { FEES_ID, fees };

		linker->fees = 0;
		tal_arr_expand(&linker->outputs, fee_out);
	} else {
		linker->fees = fees;
	}

	/* Step 1: deterministic links (skipped when intrafees blur the
	 * matching; the precheck assumes exact balances).
	 */
	if ((options & OPT_PRECHECK) && within_limits(linker) &&
	    !linker->has_intrafees) {
		prepare_data(linker);
		match_aggregates_by_value(linker);
		dtrm_links = check_deterministic_links(linker);

		res->n_rows = tal_count(linker->outputs);
		res->n_cols = tal_count(linker->inputs);
		res->matrix = tal_arrz(res, u64, res->n_rows * res->n_cols);
		dtrm_sets = tal_arr(linker, struct id_set, tal_count(dtrm_links));
		for (i = 0; i < tal_count(dtrm_links); i++) {
			const struct link_coord *lc = &dtrm_links[i];

			res->matrix[lc->row * res->n_cols + lc->col] = 1;
			dtrm_sets[i].ids = tal_arr(dtrm_sets, const char *, 2);
			dtrm_sets[i].ids[0] = linker->outputs[lc->row].id;
			dtrm_sets[i].ids[1] = linker->inputs[lc->col].id;
		}
	}

	/* Step 2: the full search. */
	n_in = tal_count(linker->inputs);
	n_out = tal_count(linker->outputs);
	if (n_in == 0 || n_out == 0) {
		/* Nothing left to link: a single all-ones combination. */
		res->nb_cmbn = 1;
		res->n_rows = n_out;
		res->n_cols = n_in;
		tal_free(res->matrix);
		res->matrix = tal_arr(res, u64, n_in * n_out);
		for (i = 0; i < n_in * n_out; i++)
			res->matrix[i] = 1;
	} else if ((options & OPT_LINKABILITY) && within_limits(linker)) {
		u64 *matrix = NULL;

		/* Inputs deterministically tied to the same output are one
		 * entity for the search; this shrinks it considerably.
		 */
		if (dtrm_sets)
			pack_linked_txos(linker, dtrm_sets);
		prepare_data(linker);
		match_aggregates_by_value(linker);
		compute_input_splits(linker);
		tal_free(res->matrix);
		res->matrix = NULL;
		if (compute_link_matrix(linker, &res->nb_cmbn, &matrix)) {
			res->matrix = tal_steal(res, matrix);
			res->n_rows = tal_count(linker->outputs);
			res->n_cols = tal_count(linker->inputs);
		} else {
			/* Time limit hit: report "not computed". */
			res->nb_cmbn = 0;
		}
	}

	res->inputs = copy_entries(res, linker->inputs);
	res->outputs = copy_entries(res, linker->outputs);
	unpack_matrix_and_txos(linker, res);
	tal_free(linker);
	return res;
}
