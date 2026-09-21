#include "boltzmann/perfect_cj.h"
#include "boltzmann/perfect_cj_table.h"

#include <math.h>

void closest_perfect_coinjoin(size_t n_in, size_t n_out,
			      size_t *pcj_in, size_t *pcj_out)
{
	size_t small = n_in, large = n_out;

	/* The shape is symmetric; work with small <= large. */
	if (small > large) {
		small = n_out;
		large = n_in;
	}
	*pcj_in = small;
	if (large % small == 0)
		*pcj_out = large;
	else
		*pcj_out = small * (1 + large / small);
}

/* n! as a double. */
static double factorial(size_t n)
{
	double f = 1;
	size_t i;

	for (i = 2; i <= n; i++)
		f *= (double)i;
	return f;
}

/**
 * count_partitions - sum, over every integer partition of @remaining
 * (parts no larger than @max_part), of the number of combinations the
 * partition stands for.
 *
 * A partition of the n_in inputs into blocks of sizes k_1..k_m describes
 * one family of combinations: each block of k inputs pays k*ratio outputs.
 * The number of set partitions with those block sizes is
 * n_in! / (prod k_j! * prod mult_k!) and, the outputs being identical too,
 * the ways to assign outputs to blocks is n_out! / prod (k_j*ratio)!.
 *
 * @block_product accumulates prod k_j! * prod (k_j*ratio)! for the parts
 * chosen so far and @mult_product the prod mult_k! (parts are chosen in
 * non-increasing order, so equal parts are adjacent and @run counts them).
 */
static double count_partitions(size_t remaining, size_t max_part, size_t ratio,
			       size_t prev_part, size_t run,
			       double block_product, double mult_product)
{
	double total = 0;
	size_t part;

	if (remaining == 0)
		return 1.0 / (block_product * mult_product * factorial(run));

	for (part = (max_part < remaining ? max_part : remaining); part >= 1; part--) {
		size_t new_run = (part == prev_part) ? run + 1 : 1;
		double new_mult = (part == prev_part) ? mult_product
						      : mult_product * factorial(run);

		total += count_partitions(remaining - part, part, ratio, part,
					  new_run,
					  block_product * factorial(part)
						  * factorial(part * ratio),
					  new_mult);
	}
	return total;
}

double perfect_coinjoin_combinations(size_t n_in, size_t n_out)
{
	size_t small = n_in, large = n_out, i;

	if (small > large) {
		small = n_out;
		large = n_in;
	}
	if (large % small != 0)
		return -1;
	if (small <= 1 || large <= 1)
		return 1;

	for (i = 0; i < ARRAY_SIZE(PERFECT_CJ_TABLE); i++) {
		if (PERFECT_CJ_TABLE[i].n_in == small &&
		    PERFECT_CJ_TABLE[i].n_out == large)
			return PERFECT_CJ_TABLE[i].nb_cmbn;
	}

	/* Not in the table: count.  run starts at 0 with a fake previous
	 * part so the first real part opens a run of 1.
	 */
	return factorial(small) * factorial(large)
	       * count_partitions(small, small, large / small, 0, 0, 1.0, 1.0);
}
