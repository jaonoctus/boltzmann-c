# Part 1: The Transaction Model

The analysis needs very little of a Bitcoin transaction: the amount of
every input and output, and an address for each so that reuse can be
detected and results can be labelled.  Every provider produces the same
model, and the analyser never learns where the data came from.

## Table of Contents

  * [The Model](#the-model)
  * [Provider Requirements](#provider-requirements)
  * [The Inline Provider](#the-inline-provider)
  * [Differences from the Reference](#differences-from-the-reference)

## The Model

A *transaction* has:

* `txid`: the hex transaction id, or the string `inline` for an inline
  transaction;
* `height`: the block height, or -1 when unknown or unconfirmed;
* `time`: the block time or first-seen time, optional;
* `inputs`: an ordered list of txos;
* `outputs`: an ordered list of txos.

A *txo* has:

* `n`: the position of the txo in the transaction that created it, or -1
  when unknown;
* `value`: the amount in satoshis, or -1 when unknown;
* `address`: a string, or absent;
* `tx_idx`: an integer only some providers have.  It is printed in the
  debug representation (Part 6) and used nowhere else.

The fee is not a field of the model.  Part 2 derives it.

## Provider Requirements

A provider:
  - MUST list inputs in the order they appear in the transaction, and
    outputs likewise.
  - MUST give every non-coinbase input the `value` and `n` of the previous
    output it spends.
  - MUST give a coinbase input `n = -1`, `value = -1`, an empty string as
    `address` and `tx_idx = -1`.
  - MUST express amounts as integers in satoshis.  A source that quotes
    amounts in BTC MUST be converted exactly, from the decimal text, and
    not through a binary floating point value.
  - MUST set `address` to the address when the output script has a
    standard address.
  - SHOULD otherwise set `address` to the hex encoding of the output
    script.
  - MAY leave `address` absent when neither is available.
  - MUST NOT drop, reorder or reject zero-value outputs.
  - MUST NOT reject a transaction for having a single input or a single
    output.

### Rationale

Only two things are ever done with an address: comparing it for equality
with other addresses of the same transaction, and printing it.  Any string
that is equal for the same script and different for different scripts will
do, hence the script hex fallback.

A coinbase input has no previous output.  The reference represents it as a
txo with every field unknown; the filtering step of Part 2 removes it, so
its exact amount never matters, but its presence in the debug
representation does.

Positions are preserved because identifiers are derived from them and the
identifiers fix the row and column order of the linkability matrix.

## The Inline Provider

An inline transaction is given as two comma-separated lists of amounts,
one for the inputs and one for the outputs.  Each element is either
`AMOUNT` or `LABEL:AMOUNT`, and `AMOUNT` is a non-negative decimal
integer.  Spaces before an element are ignored.

The inline provider:
  - MUST use the label, when given, as the txo's `address`.
  - MUST otherwise label the input at position `n` with the lowercase
    letter `a` + `n` for `n < 26`, and with `in<n+1>` beyond.
  - MUST otherwise label the output at position `n` with the uppercase
    letter `A` + `n` for `n < 26`, and with `out<n+1>` beyond.
  - MUST set `n` to the position in the list, `height` to -1, `time` to
    absent, `tx_idx` to absent and `txid` to `inline`.
  - MUST reject an empty label, an empty list, an amount that is not a
    non-negative integer, and a transaction whose outputs sum to more
    than its inputs.

### Rationale

Repeating a label is how address reuse is expressed, which is what the
`MERGE_INPUTS` option of Part 2 acts on.  A negative fee cannot occur on
chain and would make the matching rule of Part 3 meaningless.

## Differences from the Reference

The reference implementation raises on an Esplora output that has no
address, and crashes on a coinbase input.  Conforming implementations
follow this document instead: the script hex fallback and the unknown txo.
Both cases concern txos that the filtering step removes, so no printed
figure changes; only whether the transaction is analysed at all.

The reference obtains amounts from bitcoind through a float.  This
document requires exact conversion.  The two agree for every amount that
fits the 8-decimal representation bitcoind prints.
