#ifndef BOLTZMANN_PERFECT_CJ_H
#define BOLTZMANN_PERFECT_CJ_H
/*
 * Perfect coinjoins: the yardstick for wallet efficiency.
 *
 * A perfect coinjoin has n_in equal inputs, n_out equal outputs, no fee,
 * and one count divides the other.  It has the most combinations any
 * transaction of that shape can have, so the ratio
 *
 *     efficiency = nb_cmbn(tx) / nb_cmbn(closest perfect coinjoin)
 *
 * says how much of the achievable ambiguity a wallet actually produced.
 */
#include "common/types.h"

/**
 * closest_perfect_coinjoin - the perfect coinjoin shape nearest to
 * (@n_in, @n_out): same smaller side, larger side rounded up to a multiple.
 */
void closest_perfect_coinjoin(size_t n_in, size_t n_out,
			      size_t *pcj_in, size_t *pcj_out);

/**
 * perfect_coinjoin_combinations - number of combinations of a perfect
 * coinjoin, as a double (the exact values overflow 64 bits early).
 *
 * Uses the precomputed table when it applies, otherwise counts directly.
 * Returns -1 if @n_out is not a multiple of @n_in (or vice versa).
 */
double perfect_coinjoin_combinations(size_t n_in, size_t n_out);

#endif /* BOLTZMANN_PERFECT_CJ_H */
