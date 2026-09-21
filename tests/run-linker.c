/*
 * Tests for the linker on hand-checkable transactions.
 */
#include "boltzmann/txos_linker.h"
#include "boltzmann/perfect_cj.h"
#include "boltzmann/tx_processor.h"
#include "common/tal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static struct txo_entry *entries(const tal_t *ctx, const char prefix,
				 const s64 *values, size_t n)
{
	struct txo_entry *e = tal_arr(ctx, struct txo_entry, n);
	size_t i;

	for (i = 0; i < n; i++) {
		e[i].id = tal_fmt(ctx, "%c%zu", prefix, i);
		e[i].value = values[i];
	}
	return e;
}

static struct linker_result *run(const tal_t *ctx, const s64 *ins, size_t n_in,
				 const s64 *outs, size_t n_out, u32 options)
{
	struct intrafees none = { 0, 0 };
	s64 fees = 0;
	size_t i;

	for (i = 0; i < n_in; i++)
		fees += ins[i];
	for (i = 0; i < n_out; i++)
		fees -= outs[i];
	return txos_linker_process(ctx, entries(ctx, 'I', ins, n_in),
				   entries(ctx, 'O', outs, n_out), fees, NULL,
				   options, none, 600, 12);
}

static u64 cell(const struct linker_result *r, size_t row, size_t col)
{
	return r->matrix[row * r->n_cols + col];
}

int main(void)
{
	tal_t *ctx = tal(NULL, char);
	u32 dflt = OPT_PRECHECK | OPT_LINKABILITY;

	/* 2x2 equal-output coinjoin: 3 combinations (both together, or
	 * either pairing), every link present in 2 of 3.
	 */
	{
		s64 ins[] = { 10000000, 10000000 };
		s64 outs[] = { 9950000, 9950000 };
		struct linker_result *r = run(ctx, ins, 2, outs, 2, dflt);

		CHECK(r->nb_cmbn == 3);
		CHECK(r->n_rows == 2 && r->n_cols == 2);
		CHECK(cell(r, 0, 0) == 2 && cell(r, 1, 1) == 2);
	}

	/* A plain payment: one combination, everything linked. */
	{
		s64 ins[] = { 10000000, 5000000 };
		s64 outs[] = { 8990000, 6000000 };
		struct linker_result *r = run(ctx, ins, 2, outs, 2, dflt);

		CHECK(r->nb_cmbn == 1);
		CHECK(cell(r, 0, 0) == 1 && cell(r, 0, 1) == 1);
		CHECK(cell(r, 1, 0) == 1 && cell(r, 1, 1) == 1);
		/* Sorted by decreasing amount. */
		CHECK(r->inputs[0].value == 10000000);
		CHECK(r->outputs[0].value == 8990000);
	}

	/* Deterministic link: the 5M input is in every combination that
	 * pays the 5M output (values checked against the Python reference:
	 * the 100000 sat fee tolerance allows 8 combinations here).
	 */
	{
		s64 ins[] = { 10000000, 10000000, 5000000 };
		s64 outs[] = { 9950000, 9950000, 5000000 };
		struct linker_result *r = run(ctx, ins, 3, outs, 3, dflt);

		CHECK(r->nb_cmbn == 8);
		/* Row 2 = 5M output, col 2 = 5M input (both sorted last). */
		CHECK(cell(r, 2, 2) == 8);
		CHECK(cell(r, 0, 2) == 3);
		CHECK(cell(r, 0, 0) == 5);
	}

	/* PRECHECK alone: 0/1 flags, no combination count. */
	{
		s64 ins[] = { 10000000, 10000000, 5000000 };
		s64 outs[] = { 9950000, 9950000, 5000000 };
		struct linker_result *r = run(ctx, ins, 3, outs, 3, OPT_PRECHECK);

		CHECK(r->nb_cmbn == 0);
		CHECK(cell(r, 2, 2) == 1 && cell(r, 0, 0) == 0);
	}

	/* Packing: two inputs declared as one entity behave as their sum,
	 * and are expanded back into two identical columns.
	 */
	{
		s64 ins[] = { 6000000, 4000000, 10000000 };
		s64 outs[] = { 9950000, 9950000 };
		struct id_set *linked = tal_arr(ctx, struct id_set, 1);
		struct intrafees none = { 0, 0 };
		struct linker_result *r;

		linked[0].ids = tal_arr(linked, const char *, 2);
		linked[0].ids[0] = "I0";
		linked[0].ids[1] = "I1";
		r = txos_linker_process(ctx, entries(ctx, 'I', ins, 3),
					entries(ctx, 'O', outs, 2), 100000, linked,
					dflt, none, 600, 12);
		CHECK(r->nb_cmbn == 3);
		CHECK(r->n_cols == 3);
		/* The pack (10M) sorts after I2 (10M, seen first: stable
		 * sort), then expands in place into I0, I1.
		 */
		CHECK(strcmp(r->inputs[0].id, "I2") == 0);
		CHECK(strcmp(r->inputs[1].id, "I0") == 0);
		CHECK(strcmp(r->inputs[2].id, "I1") == 0);
		CHECK(cell(r, 0, 1) == cell(r, 0, 2));
	}

	/* Perfect coinjoin counts: table and direct computation agree. */
	CHECK(perfect_coinjoin_combinations(2, 4) == 7);
	CHECK(perfect_coinjoin_combinations(3, 3) == 16);
	CHECK(perfect_coinjoin_combinations(12, 12) == 15024619744202.0);
	/* Not in the table: C(60, 30) + 1, computed directly. */
	CHECK(fabs(perfect_coinjoin_combinations(2, 60) / 118264581564861425.0 - 1)
	      < 1e-12);
	{
		size_t in, out;

		closest_perfect_coinjoin(5, 12, &in, &out);
		CHECK(in == 5 && out == 15);
		closest_perfect_coinjoin(12, 5, &in, &out);
		CHECK(in == 5 && out == 15);
	}
	CHECK(compute_wallet_efficiency(2, 2, 3) == 1.0);
	CHECK(compute_wallet_efficiency(2, 2, 1) == 0.0);

	tal_free(ctx);
	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
