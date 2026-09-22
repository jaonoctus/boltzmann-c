# Part 2: Parameters, Filtering, Packing and the Order of Operations

This part is procedural.  The steps below are numbered because their order
is observable: identifiers depend on filtering happening first, the fee on
which txos were filtered, the printed input order on when packing and
sorting happen, and the efficiency score on counts taken before packing.
An implementation MAY organise its code differently as long as every
observable result is the one this order produces.

## Table of Contents

  * [Parameters](#parameters)
  * [Step 1: Filter and Identify](#step-1-filter-and-identify)
  * [Step 2: Fee](#step-2-fee)
  * [Step 3: Trivial Transactions](#step-3-trivial-transactions)
  * [Step 4: Linked Input Sets](#step-4-linked-input-sets)
  * [Step 5: Hypothesis](#step-5-hypothesis)
  * [Step 6: Pack Linked Inputs](#step-6-pack-linked-inputs)
  * [Step 7: Fee Output](#step-7-fee-output)
  * [Step 8: Limits](#step-8-limits)
  * [Step 9: Deterministic-Link Pass](#step-9-deterministic-link-pass)
  * [Step 10: Combination Pass](#step-10-combination-pass)
  * [Step 11: Unpack](#step-11-unpack)
  * [Step 12: Efficiency](#step-12-efficiency)
  * [Step 13: Label](#step-13-label)

## Parameters

| parameter          | default                              | meaning |
|--------------------|--------------------------------------|---------|
| `options`          | `PRECHECK, LINKABILITY, MERGE_INPUTS` | set of processing options |
| `max_duration`     | 600                                  | seconds allowed for the combination count |
| `max_txos`         | 12                                   | largest working list that is analysed |
| `cj_max_fee_ratio` | 0                                    | intrafees tolerance, as a fraction (Part 5); 0 disables |

The options are:

* `PRECHECK`: find deterministic links without counting combinations.
* `LINKABILITY`: count combinations and fill the linkability matrix.
* `MERGE_INPUTS`: treat inputs sharing an address as one entity.
* `MERGE_OUTPUTS`: accepted, no effect.
* `MERGE_FEES`: treat the fee as one more output paid by one entity.

### Requirements

The analyser:
  - MUST accept the five option names above, comma-separated, ignoring
    spaces before a name.
  - MUST reject any other name.
  - MUST treat `MERGE_OUTPUTS` as having no effect on any result.

### Rationale

The reference only ever packs inputs; output sets are computed and then
never matched against anything.  The option is kept so that command lines
written for the reference keep working.  `cj_max_fee_ratio` is documented
by the reference as a percentage but used as a fraction: `0.005` means
half a percent.

## Step 1: Filter and Identify

The analyser:
  - MUST keep, from the provider's input list, every txo whose amount is
    strictly positive, in provider order, and give the txo at provider
    position `n` the identifier `I<n>`.
  - MUST do the same for outputs with the identifier `O<n>`.
  - MUST count positions over the provider's full list, dropped txos
    included.

The two results are the *filtered lists*.  Their sizes are `n_in` and
`n_out`.

### Rationale

A coinbase input has an unknown amount (-1) and an OP_RETURN output
carries nothing; neither can take part in a story about who paid whom.
Identifiers keep the original position so that the same txo has the same
name whatever was filtered around it.

## Step 2: Fee

The analyser:
  - MUST compute `fee` as the sum of the filtered inputs minus the sum of
    the filtered outputs.

## Step 3: Trivial Transactions

If `n_in <= 1` or `n_out = 1`, the analyser:
  - MUST set `nb_cmbn = 1` and produce no matrix.
  - MUST report the filtered lists in provider order.
  - MUST skip to step 12.

### Rationale

One input paying everything, or everything paying one output, admits a
single story.  The reference returns early here, before any sorting, and
the unsorted order is visible in the printed lists.

## Step 4: Linked Input Sets

If `MERGE_INPUTS` is set, the analyser MUST build an ordered list of
groups from the filtered inputs as follows.  Groups are tagged with an
address.  For each filtered input in order:

  1. Start a new group containing that input, tagged with its address.
  2. If an existing group carries the same address, move its members into
     the new group and remove the existing group from the list.
  3. Append the new group at the end of the list.

The *linked sets* are the groups with more than one member, in list order.
A set is unordered; the order of its members never matters.

If `MERGE_INPUTS` is not set, there are no linked sets.

### Rationale

Inputs spending from one address were signed by one key.  The order of
the groups fixes the order in which packs are created in step 6, and
therefore the tie-breaking between packs of equal amount when they are
sorted.

## Step 5: Hypothesis

If `cj_max_fee_ratio > 0`, the analyser:
  - MUST compute the number of entities `E` among the filtered inputs:
    every input belongs to exactly one entity, and two inputs share an
    entity if and only if some linked set contains both.
  - MUST run the pattern detection of Part 5 on the filtered outputs, in
    provider order, with `E` as the maximum number of participants.
  - MUST, when a pattern is found, adopt the hypothesis `(maker, taker)`
    that Part 5 derives from it.

Otherwise, and when no pattern is found, there is no hypothesis.

## Step 6: Pack Linked Inputs

The *working input list* starts as the filtered input list.  Packs are
numbered from 1 across the whole analysis, including the packs of
step 10.

For each linked set in order, the analyser:
  - MUST collect the entries of the working input list whose identifier
    is in the set, in working list order.
  - MUST, if there are none, do nothing and consume no pack number.
  - MUST otherwise remove those entries, append to the working list a
    synthetic input with identifier `PACK_I<k>` (`k` the next pack
    number) and amount the sum of the removed entries, and record the
    removed entries as the pack's members in the order collected.

Sets that share an identifier MUST first be merged into one set, keeping
the position of the earliest.

### Rationale

Appending the pack at the end is observable only when no later step
sorts the list (a transaction skipped for size), and then the printed
input list shows the pack's members after every other input.

## Step 7: Fee Output

The *working output list* starts as the filtered output list.

If `MERGE_FEES` is set and `fee > 0`, the analyser:
  - MUST append to the working output list a synthetic output with
    identifier `FEES` and amount `fee`.
  - MUST use `0` as the matching fee `f` of Part 3.

Otherwise the analyser MUST use `fee` as the matching fee.

### Rationale

Once the miner is a participant, every sub-transaction balances exactly.
The synthetic output stays in the matrix (one extra row) but is not
printed in `Outputs`, as in the reference.

## Step 8: Limits

The analyser:
  - MUST compute `within = max(|working inputs|, |working outputs|)
    <= max_txos` on the lists as they stand after steps 6 and 7.

## Step 9: Deterministic-Link Pass

This step runs if and only if `PRECHECK` is set, `within` holds, and
there is no hypothesis.

The analyser:
  - MUST prepare both working lists (Part 3).
  - MUST compute the deterministic links of Part 3 with the matching fee
    `f`.
  - MUST build a matrix over the prepared lists with a `1` in every cell
    that is a deterministic link and `0` elsewhere.  This is the result
    matrix unless step 10 replaces it.
  - MUST, for every deterministic link, form the set of the two
    identifiers (the output's and the input's).

### Rationale

The 0/1 matrix is what gets printed when only `PRECHECK` was requested.
The sets feed step 10: two inputs deterministically linked to the same
output end up in one set because both sets contain that output's
identifier.

## Step 10: Combination Pass

If either working list is empty, the analyser:
  - MUST set `nb_cmbn = 1` and fill a matrix of the lists' dimensions
    with `1`.

Otherwise, if `LINKABILITY` is set and `within` holds, the analyser:
  - MUST, if step 9 ran, pack the sets it formed exactly as in step 6.
    Only input identifiers are ever packed; an output identifier in a set
    is ignored.  Pack numbering continues from step 6.
  - MUST prepare both working lists again.
  - MUST count combinations and fill the linkability matrix (Part 3) with
    the matching fee `f` and the hypothesis, if any.
  - MUST, if `max_duration` is exceeded, set `nb_cmbn = 0` and discard
    any matrix, the one from step 9 included.

Otherwise (`LINKABILITY` unset, or `within` false) the analyser:
  - MUST set `nb_cmbn = 0` and keep the matrix of step 9 if that step
    ran, else produce no matrix.

### Rationale

Packing deterministically linked inputs does not change the count, but it
does change the sorted order of the input list: the pack sorts by its
summed amount and is expanded in place in step 11.  This is why the
packing is required rather than merely allowed.

An empty side can only arise from a provider that reports every output as
zero-value, since step 3 has already handled a single output.

## Step 11: Unpack

The *reported lists* start as the working lists as they stand: prepared
(sorted) if step 9 or step 10 prepared them, otherwise in post-packing
order.

For each pack in reverse order of creation, the analyser:
  - MUST locate the pack's entry in the reported input list by
    identifier.
  - MUST replace that entry with the pack's members, in the order
    recorded, at the same position.
  - MUST, if there is a matrix, replace the pack's column with one copy
    per member.

### Rationale

Everything true of the pack is true of each member, so its column is
duplicated.  A pack from step 10 may contain a pack from step 6, hence
the reverse order.

## Step 12: Efficiency

The analyser:
  - MUST compute the wallet efficiency of Part 4 from `nb_cmbn` and the
    filtered counts `n_in` and `n_out` of step 1.

### Rationale

The closest perfect coinjoin is chosen by the shape of the transaction
as it exists on chain, before any packing and without the fee output.

## Step 13: Label

For the printer, the analyser:
  - MUST replace each identifier in the reported lists by the address of
    the corresponding txo, or by "absent" when the provider gave none.
  - MUST drop synthetic entries (`PACK_I<k>`, `FEES`) from the reported
    lists.
  - MUST NOT drop any row or column of the matrix.

### Rationale

After step 11 no pack remains in the input list, so in practice only the
fee output is dropped, leaving a matrix with one more row than there are
printed outputs.  The reference behaves this way.
