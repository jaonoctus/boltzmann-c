#include "boltzmann/perfect_cj.h"
#include "boltzmann/tx_processor.h"
#include "common/pyfmt.h"
#include "common/utils.h"

#include <stdio.h>

/**
 * struct id_addr - maps a linker id ("I3") back to the txo's address.
 */
struct id_addr {
	const char *id;
	const char *address;
};

/**
 * filter_txos - keep txos with a positive amount and give each an id.
 *
 * Zero-value outputs (OP_RETURN) carry no coins and take no part in the
 * analysis.  Ids are "I<n>" / "O<n>" with n the txo's original position.
 */
static struct txo_entry *filter_txos(const tal_t *ctx, struct txo **txos,
				     char prefix, struct id_addr **map)
{
	struct txo_entry *entries = tal_arr(ctx, struct txo_entry, 0);
	size_t i;

	*map = tal_arr(ctx, struct id_addr, 0);
	for (i = 0; i < tal_count(txos); i++) {
		struct txo_entry e;
		struct id_addr ia;

		if (txos[i]->value <= 0)
			continue;
		e.id = tal_fmt(ctx, "%c%zu", prefix, i);
		e.value = txos[i]->value;
		tal_arr_expand(&entries, e);
		ia.id = e.id;
		ia.address = txos[i]->address;
		tal_arr_expand(map, ia);
	}
	return entries;
}

static const struct id_addr *lookup_id(const struct id_addr *map,
				       const char *id)
{
	size_t i;

	for (i = 0; i < tal_count(map); i++) {
		if (streq(map[i].id, id))
			return &map[i];
	}
	return NULL;
}

/**
 * get_linked_txos - group txos by address; returns the groups of size > 1.
 *
 * Txos sharing an address are assumed to belong to one entity.  The group
 * order matters downstream (it fixes the order of equal-valued packs), so
 * it mirrors the reference: each time an address is seen again its group
 * moves to the end of the list.
 */
static struct id_set *get_linked_txos(const tal_t *ctx,
				      const struct txo_entry *entries,
				      const struct id_addr *map)
{
	struct id_set *groups = tal_arr(ctx, struct id_set, 0);
	const char **group_addr = tal_arr(ctx, const char *, 0);
	struct id_set *linked = tal_arr(ctx, struct id_set, 0);
	size_t i, g;

	for (i = 0; i < tal_count(entries); i++) {
		const char *addr = lookup_id(map, entries[i].id)->address;
		struct id_set group;

		group.ids = tal_arr(groups, const char *, 0);
		tal_arr_expand(&group.ids, entries[i].id);

		for (g = 0; g < tal_count(groups); g++) {
			if (!streq(group_addr[g], addr))
				continue;
			/* Absorb the existing group and drop it from its
			 * place; the merged group is appended below.
			 */
			for (size_t k = 0; k < tal_count(groups[g].ids); k++)
				tal_arr_expand(&group.ids, groups[g].ids[k]);
			for (size_t k = g + 1; k < tal_count(groups); k++) {
				groups[k - 1] = groups[k];
				group_addr[k - 1] = group_addr[k];
			}
			tal_resize(&groups, tal_count(groups) - 1);
			tal_resize(&group_addr, tal_count(group_addr) - 1);
			break;
		}
		tal_arr_expand(&groups, group);
		tal_arr_expand(&group_addr, addr);
	}

	for (g = 0; g < tal_count(groups); g++) {
		if (tal_count(groups[g].ids) > 1)
			tal_arr_expand(&linked, groups[g]);
	}
	return linked;
}

bool check_coinjoin_pattern(const struct txo_entry *outputs,
			    size_t max_nb_entities,
			    size_t *nb_ptcpts, s64 *cj_amount)
{
	size_t n_out = tal_count(outputs), i, j;
	bool is_cj = false;
	size_t best_ptcpts = 0;
	s64 best_amount = 0;

	/* A coinjoin needs at least two participants. */
	if (max_nb_entities < 2)
		return false;

	/* For every distinct output amount, count how often it occurs.
	 * Amounts are visited in first-occurrence order, as a dict would.
	 */
	for (i = 0; i < n_out; i++) {
		size_t occurrences = 0, ptcpts;
		bool seen_before = false;

		for (j = 0; j < i; j++) {
			if (outputs[j].value == outputs[i].value)
				seen_before = true;
		}
		if (seen_before)
			continue;
		for (j = 0; j < n_out; j++) {
			if (outputs[j].value == outputs[i].value)
				occurrences++;
		}
		if (occurrences <= 1)
			continue;

		/* n equal outputs suggests n participants, each with at most
		 * one change output.  Prefer more participants, then a
		 * larger amount.
		 */
		ptcpts = occurrences < max_nb_entities ? occurrences : max_nb_entities;
		if (n_out <= 2 * ptcpts && ptcpts >= best_ptcpts &&
		    outputs[i].value > best_amount) {
			is_cj = true;
			best_ptcpts = ptcpts;
			best_amount = outputs[i].value;
		}
	}
	*nb_ptcpts = best_ptcpts;
	*cj_amount = best_amount;
	return is_cj;
}

/**
 * compute_coinjoin_intrafees - the JoinMarket-style fee hypothesis: the
 * taker pays each of the (nb_ptcpts - 1) makers @prct_max of the amount.
 */
static struct intrafees compute_coinjoin_intrafees(size_t nb_ptcpts,
						   s64 cj_amount, double prct_max)
{
	struct intrafees fees;

	fees.maker = (double)cj_amount * prct_max;
	fees.taker = fees.maker * (double)(nb_ptcpts - 1);
	return fees;
}

double compute_wallet_efficiency(size_t n_in, size_t n_out, u64 nb_cmbn)
{
	size_t pcj_in, pcj_out;
	double pcj_cmbn;

	if (nb_cmbn == 1)
		return 0;
	closest_perfect_coinjoin(n_in, n_out, &pcj_in, &pcj_out);
	pcj_cmbn = perfect_coinjoin_combinations(pcj_in, pcj_out);
	return (double)nb_cmbn / pcj_cmbn;
}

/**
 * label_txos - replace ids by addresses.  Synthetic txos (packs, fees)
 * have no address and are dropped, as in the reference implementation.
 */
static struct labelled_txo *label_txos(const tal_t *ctx,
				       const struct txo_entry *entries,
				       const struct id_addr *map)
{
	struct labelled_txo *out = tal_arr(ctx, struct labelled_txo, 0);
	size_t i;

	for (i = 0; i < tal_count(entries); i++) {
		const struct id_addr *ia = lookup_id(map, entries[i].id);
		struct labelled_txo l;

		if (!ia)
			continue;
		l.address = ia->address ? tal_strdup(out, ia->address) : NULL;
		l.value = entries[i].value;
		tal_arr_expand(&out, l);
	}
	return out;
}

struct tx_analysis *process_tx(const tal_t *ctx, const struct transaction *tx,
			       u32 options, double max_duration, size_t max_txos,
			       double max_cj_intrafees_ratio)
{
	double start = time_now_seconds();
	struct tx_analysis *an = talz(ctx, struct tx_analysis);
	tal_t *tmp = tal(ctx, char);
	struct id_addr *map_ins, *map_outs;
	struct txo_entry *ins, *outs, *result_ins, *result_outs;
	s64 sum_in = 0, sum_out = 0;
	size_t i;

	ins = filter_txos(tmp, tx->inputs, 'I', &map_ins);
	outs = filter_txos(tmp, tx->outputs, 'O', &map_outs);
	for (i = 0; i < tal_count(ins); i++)
		sum_in += ins[i].value;
	for (i = 0; i < tal_count(outs); i++)
		sum_out += outs[i].value;
	an->fees = sum_in - sum_out;
	an->intrafees.maker = 0;
	an->intrafees.taker = 0;

	if (tal_count(ins) <= 1 || tal_count(outs) == 1) {
		/* Coinbase, or a single input/output: everything is linked to
		 * everything and there is exactly one combination.  No matrix
		 * is built; the display knows what a NULL matrix means.
		 */
		an->matrix = NULL;
		an->nb_cmbn = 1;
		result_ins = ins;
		result_outs = outs;
	} else {
		struct id_set *linked_ins, *linked_outs, *linked;
		struct linker_result *res;

		linked_ins = (options & OPT_MERGE_INPUTS)
				     ? get_linked_txos(tmp, ins, map_ins)
				     : tal_arr(tmp, struct id_set, 0);
		/* Merging outputs is accepted for compatibility: the reference
		 * implementation only ever packs *inputs*, so these sets have
		 * no effect on the analysis.
		 */
		linked_outs = (options & OPT_MERGE_OUTPUTS)
				      ? get_linked_txos(tmp, outs, map_outs)
				      : tal_arr(tmp, struct id_set, 0);

		if (max_cj_intrafees_ratio > 0) {
			/* The number of distinct input entities bounds the
			 * number of coinjoin participants.
			 */
			struct id_set *entities;
			size_t nb_ptcpts;
			s64 cj_amount;

			entities = tal_arr(tmp, struct id_set, 0);
			for (i = 0; i < tal_count(linked_ins); i++)
				tal_arr_expand(&entities, linked_ins[i]);
			for (i = 0; i < tal_count(ins); i++) {
				struct id_set single;

				single.ids = tal_arr(tmp, const char *, 1);
				single.ids[0] = ins[i].id;
				tal_arr_expand(&entities, single);
			}
			entities = merge_sets(tmp, entities);
			if (check_coinjoin_pattern(outs, tal_count(entities),
						   &nb_ptcpts, &cj_amount))
				an->intrafees = compute_coinjoin_intrafees(
					nb_ptcpts, cj_amount, max_cj_intrafees_ratio);
		}

		linked = tal_arr(tmp, struct id_set, 0);
		for (i = 0; i < tal_count(linked_ins); i++)
			tal_arr_expand(&linked, linked_ins[i]);
		for (i = 0; i < tal_count(linked_outs); i++)
			tal_arr_expand(&linked, linked_outs[i]);

		res = txos_linker_process(tmp, ins, outs, an->fees, linked, options,
					  an->intrafees, max_duration, max_txos);
		an->nb_cmbn = res->nb_cmbn;
		an->n_rows = res->n_rows;
		an->n_cols = res->n_cols;
		an->matrix = res->matrix ? tal_steal(an, res->matrix) : NULL;
		result_ins = res->inputs;
		result_outs = res->outputs;
	}

	an->efficiency = compute_wallet_efficiency(tal_count(ins),
						   tal_count(outs), an->nb_cmbn);
	an->inputs = label_txos(an, result_ins, map_ins);
	an->outputs = label_txos(an, result_outs, map_outs);
	an->duration = time_now_seconds() - start;
	tal_free(tmp);

	{
		char *dur = py_float_str(an, an->duration);

		printf("Duration = %s\n", dur);
		tal_free(dur);
	}
	return an;
}
