# Part 3: Aggregates, Matching, Deterministic Links and Combinations

This part defines what the analyser computes from a prepared pair of
working lists: which aggregates match, which links are deterministic, how
many combinations exist and how often each link occurs.  It defines
results, not procedures.  Any algorithm that produces these exact figures
conforms.  Part 2 says when each computation runs and on which lists.

## Table of Contents

  * [Prepared Lists and Aggregates](#prepared-lists-and-aggregates)
  * [Matching Rule](#matching-rule)
  * [Deterministic Links](#deterministic-links)
  * [Combinations and the Linkability Matrix](#combinations-and-the-linkability-matrix)
  * [Limits](#limits)
  * [Result States](#result-states)

## Prepared Lists and Aggregates

A working list is *prepared* when every entry with a non-positive amount
has been removed and the remainder has been sorted by decreasing amount
with a stable sort: entries of equal amount keep their relative order.

An *aggregate* of a prepared list is any subset of its entries, the empty
subset included.  Its *sum* is the total amount of its entries.  The *full
aggregate* is the whole list.

### Requirements

The analyser:
  - MUST use a stable sort.
  - MUST consider every subset of each list as an aggregate.

### Rationale

Everything downstream is indexed by position in the prepared list: the
matrix rows and columns, the printed `Inputs` and `Outputs`, the order
in which deterministic links are listed.  Stability is what makes a pack
of the same amount as a real input sort after it, which is visible in the
printed input list.

## Matching Rule

Let `in_sum` be the sum of an input aggregate, `out_sum` the sum of an
output aggregate, `f` the matching fee fixed by Part 2 (the transaction
fee, or zero under `MERGE_FEES`), and `diff = in_sum - out_sum`.

### Requirements

Without a hypothesis, the analyser:
  - MUST treat the two aggregates as a matched pair if and only if
    `0 <= diff <= f`.

With a hypothesis `(maker, taker)` from Part 5, the analyser:
  - MUST treat the two aggregates as a matched pair if and only if
    `-maker <= diff <= 0` or `0 <= diff <= f + taker`, the comparisons
    being made in double precision floating point.

### Rationale

Within one sub-transaction, the inputs pay the outputs and whatever is left
goes to the miner.  No sub-transaction can leave more than the whole fee.
Under the hypothesis a maker may come out ahead by up to `maker` and a
taker may pay up to `taker` on top of its share of the fee.  The
hypothesis values are products of an amount and a ratio, hence not
integers; the reference compares them as floats.

## Deterministic Links

Let `M` be the set of all matched pairs `(A, B)` of a prepared input list
and a prepared output list.  For an input `i` and an output `o`, let
`count(i, o)` be the number of pairs in `M` with `i` in `A` and `o` in
`B`.  Let `i0` be the first input of the prepared list, and `N` the
number of pairs in `M` with `i0` in `A`.

### Requirements

The analyser:
  - MUST report `(i, o)` as a deterministic link if and only if
    `count(i, o) = N`.
  - MUST NOT compute deterministic links while a hypothesis is active.
  - MUST list deterministic links in row-major order of the matrix: by
    output position, then by input position.

### Rationale

This is the Coinjoin Sudoku argument (K. Atlas): an input takes part in
some number of matched pairs, and an output that appears in every one of
them is tied to that input whatever the true story is.  The reference
reads that number off the first input and applies it to every input, and
this is normative: an implementation that counted per input would
disagree with it on asymmetric transactions.

The tolerance introduced by a hypothesis blurs the sums, and the reference
skips this step in that case.

## Combinations and the Linkability Matrix

A *combination* is a set of matched pairs such that every input of the
prepared input list belongs to exactly one pair and every output of the
prepared output list belongs to exactly one pair.  Two combinations are
distinct if and only if their sets of pairs differ.

The *linkability matrix* has one row per output and one column per input,
in prepared order.  The cell `(o, i)` is the number of combinations
containing a pair `(A, B)` with `i` in `A` and `o` in `B`.

### Requirements

The analyser:
  - MUST count every combination exactly once, the combination made of
    the single pair (full input aggregate, full output aggregate)
    included.
  - MUST report that count as `nb_cmbn`.
  - MUST fill every cell of the linkability matrix as defined above.
  - MAY compute these figures by any method, including methods that
    first merge inputs deterministically linked to the same output, as
    long as the figures are unchanged.

### Rationale

The single-pair combination is the story in which one party owns
everything.  It always exists since the full aggregates differ by exactly
the fee, and it links every input to every output once.

Merging inputs that share a deterministic link with one output is safe:
every combination already places them in the pair that contains that
output.  Part 2 makes this particular merge mandatory, because it changes
the order of the printed input list.

Counts are exact non-negative integers.  The reference uses arbitrary
precision integers; the default limit of 12 txos keeps every count below
2^64.  An implementation with fixed-width counters MUST NOT be run beyond
the shapes its counters can hold.

## Limits

Two parameters bound the work.  `max_txos` (default 12) bounds the size of
the working lists; `max_duration` (default 600 seconds) bounds the time
spent counting combinations.

### Requirements

The analyser:
  - MUST measure `max_txos` against the larger of the two working lists
    as they stand after packing and after the fee output is added, but
    before deterministic-link packing.
  - MUST NOT compute deterministic links or combinations when that size
    exceeds `max_txos`.
  - MUST abandon the combination count once `max_duration` seconds have
    elapsed since the count began, reporting `nb_cmbn = 0` and no matrix.
  - MAY check the time at any granularity.

## Result States

The pair (`nb_cmbn`, matrix) takes one of four forms, which the printer of
Part 6 tells apart:

| `nb_cmbn` | matrix | meaning |
|-----------|--------|---------|
| 1         | none   | trivial transaction (Part 2, step 3) |
| 0         | none   | skipped: over `max_txos`, or out of time |
| 0         | 0/1 flags | deterministic links only (`PRECHECK` without a count) |
| >= 1      | counts | full result |
