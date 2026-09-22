# Part 4: Entropy, Efficiency and the Deterministic Link Ratio

The figures printed after the combination count are all derived from
`nb_cmbn`, the matrix and the sizes of the lists.  This part defines each
one and says when it exists; Part 6 says how it is printed.

## Table of Contents

  * [Entropy](#entropy)
  * [Wallet Efficiency](#wallet-efficiency)
  * [Entropy Density](#entropy-density)
  * [Deterministic Links and their Ratio](#deterministic-links-and-their-ratio)

## Entropy

### Requirements

When `nb_cmbn > 0`, the analyser:
  - MUST compute the entropy as `log2(nb_cmbn)`, in bits.

When `nb_cmbn = 0`, there is no entropy.

## Wallet Efficiency

A *perfect coinjoin* of shape `(p, q)` with `p <= q` and `q = r * p` has
`p` equal inputs, `q` equal outputs and no fee.  Its number of
combinations, `PCJ(p, q)`, is the largest any transaction of that shape
can have, and is:

    PCJ(p, q) = p! * q! * SUM over partitions (k_1 >= ... >= k_m) of p
                of 1 / ( PROD_j k_j! * PROD_j (r * k_j)! * PROD_v m_v! )

where the sum runs over the integer partitions of `p`, and `m_v` is the
number of parts equal to `v`.  `PCJ(p, q) = 1` when `p <= 1`.  The shape
is symmetric: `PCJ(q, p) = PCJ(p, q)`.

The *closest perfect coinjoin* to a transaction with `n_in` filtered
inputs and `n_out` filtered outputs has `small = min(n_in, n_out)` on one
side and, on the other, `large = max(n_in, n_out)` if `small` divides it,
else `small * (1 + floor(large / small))`.

### Requirements

The analyser:
  - MUST set the efficiency to `0` when `nb_cmbn <= 1`.
  - MUST otherwise set it to `nb_cmbn / PCJ(small, large)` for the closest
    perfect coinjoin, the division being performed in IEEE binary64.
  - MUST, for every shape with `small <= 20` and `large <= 60`, use for
    `PCJ` the exact integer rounded to the nearest binary64 value.
  - MAY evaluate `PCJ` for other shapes by any method accurate to within
    one unit in the last place of binary64.

A few values, as a check:

| shape    | PCJ |
|----------|-----|
| (2, 2)   | 3 |
| (2, 4)   | 7 |
| (3, 3)   | 16 |
| (12, 12) | 15024619744202 |

### Rationale

Each block of `k` inputs in a set partition of the inputs pays `k * r`
outputs.  The number of set partitions with given block sizes is
`p! / (PROD k_j! * PROD m_v!)` and, the outputs being interchangeable too,
the outputs are assigned to blocks in `q! / PROD (r * k_j)!` ways.

The reference computes `PCJ` as an exact integer and converts it to a
float at the division.  Counts above 2^53 lose precision at that point,
so an implementation that computed the sum in floating point could
differ in the last digits printed; the table requirement pins it.

## Entropy Density

Let `d_in` and `d_out` be the sizes of the reported lists after step 13
of Part 2: packs expanded, synthetic txos dropped.

### Requirements

When `nb_cmbn > 0`, the analyser:
  - MUST compute the entropy density as `log2(nb_cmbn) / (d_in + d_out)`.

### Rationale

This is bits per txo.  The reference prints it followed by a percent
sign without multiplying by 100, and Part 6 requires the same.  The
figure is not scaled to look like a percentage; only the sign is wrong.

## Deterministic Links and their Ratio

### Requirements

When there is a matrix, the analyser:
  - MUST report the pair (input `i`, output `o`) as a deterministic link
    if and only if `cell(o, i) = nb_cmbn` and `cell(o, i) != 0`,
    considering only the rows that correspond to reported outputs (the
    `FEES` row, if any, is excluded) and all columns.
  - MUST list the links by output position, then by input position.
  - MUST compute the deterministic link ratio as
    `links / (d_out * d_in) * 100`.

### Rationale

A link present in every combination is certain.  With a full result
this is the cell equal to the count.  When only `PRECHECK` ran,
`nb_cmbn` is 0 and the second condition excludes every cell, so no link
is listed and the ratio is 0 although the matrix shows the flags.  This
is the reference's behaviour and is normative.
