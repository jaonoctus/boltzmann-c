# Part 5: The Coinjoin Intrafees Hypothesis

In a coinjoin where a taker pays the makers for their liquidity, the
sub-transactions no longer balance exactly: each maker leaves with a bit
more than it brought and the taker with a bit less.  With exact matching,
such a transaction has a single combination.  The hypothesis widens the
matching rule of Part 3 by a bounded amount, derived from the transaction
itself and a user-supplied ratio.

## Table of Contents

  * [Parameter](#parameter)
  * [Pattern Detection](#pattern-detection)
  * [The Hypothesis](#the-hypothesis)
  * [Effects](#effects)

## Parameter

`cj_max_fee_ratio` is the largest fee a taker is assumed to pay one maker,
as a fraction of the coinjoined amount.  `0` disables everything in this
part.  The reference's help text calls it a percentage; it is a fraction:
`0.005` is half a percent.

## Pattern Detection

The detection runs on the filtered outputs in provider order (Part 2,
step 5) with `E`, the number of input entities, as the maximum number of
participants.  Let `n_out` be the number of filtered outputs.

### Requirements

The analyser:
  - MUST report no pattern when `E < 2`.
  - MUST visit each distinct output amount `v` once, in order of first
    occurrence, and let `c` be the number of outputs of amount `v`.
  - MUST skip amounts with `c <= 1`.
  - MUST compute `p = min(c, E)`.
  - MUST accept `(p, v)` as the current best when `n_out <= 2 * p`, `p` is
    greater than or equal to the best `p` so far, and `v` is strictly
    greater than the best `v` so far (both starting at 0).
  - MUST report a pattern if and only if some amount was accepted, with
    the final best `(p, v)` as the number of participants and the
    coinjoined amount.

### Rationale

`c` equal outputs suggest `c` participants, but there cannot be more
participants than input entities.  Each participant has at most one
change output, hence `n_out <= 2 * p`.  Among candidates the reference
prefers more participants, then a larger amount; the strict inequality on
the amount means a later amount with the same `p` must be larger to win.

## The Hypothesis

### Requirements

Given a pattern `(p, v)`, the analyser:
  - MUST set `maker = v * cj_max_fee_ratio`.
  - MUST set `taker = maker * (p - 1)`.
  - MUST carry both as binary64 values, without rounding to satoshis.

### Rationale

Each of the `p - 1` makers may receive up to `maker`; the taker pays all
of them.  Truncation to an integer happens only when the two figures are
printed (Part 6).

## Effects

While a hypothesis is active:
  - the matching rule of Part 3 uses its widened form;
  - deterministic links are not computed (Part 2, step 9 does not run);
  - the printer emits the two `Hypothesis:` lines of Part 6.

A `cj_max_fee_ratio > 0` with no detected pattern has no effect at all.
