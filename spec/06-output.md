# Part 6: Textual Output Format and Test Vectors

The output is the contract.  Everything the analyser computes reaches the
user as text, and two implementations conform to each other exactly when
that text is identical.  The format is the one the reference produces with
Python's `%` formatting, `str(float)` and numpy's array printer; this part
restates those rules so that no Python is needed to follow them.

## Table of Contents

  * [Overall Structure](#overall-structure)
  * [Number Formats](#number-formats)
  * [Debug Representation](#debug-representation)
  * [Result Block](#result-block)
  * [Matrix Format](#matrix-format)
  * [Appendix A: Inline Test Vectors](#appendix-a-inline-test-vectors)
  * [Appendix B: On-Chain Test Vectors](#appendix-b-on-chain-test-vectors)

## Overall Structure

A run prints one provider line, then one section per requested txid.  In
the templates below `\n` is a newline and everything else is literal.

    DEBUG: Using <description>\n

The provider descriptions are `remote blockchain.info API`,
`remote Blockstream API`, `remote mempool.space API`,
`local RPC interface`, `local file`, `standard input` and
`inline transaction`.

Each section is:

    \n\n--- <txid> -------------------------------------\n

followed either by the failure line

    Unable to retrieve information for <txid> from <description>: <error>\n

or by

    DEBUG: Tx fetched: <debug representation>\n
    Duration = <seconds>\n
    <result block>

### Requirements

The printer:
  - MUST print the `Duration` line with the elapsed seconds as a Python
    `str(float)`.  Its value is the only part of the output that need not
    match between implementations.
  - MUST print the section header with exactly the punctuation above:
    three dashes, a space, the txid, a space and 37 dashes.

## Number Formats

* `%lld`: a decimal integer, minus sign if negative.
* `%f`: fixed notation with exactly six digits after the point, as C's
  `printf("%f")`.
* `str(float)`: the shortest decimal that round-trips to the same
  binary64 value; fixed notation when the decimal exponent is in
  `[-4, 16)`, always with a fractional part (`1.0`, `0.5`), otherwise
  `1e-05` style with at least two exponent digits.

## Debug Representation

The transaction of Part 1 is printed as:

    { 'height': <h>, 'time':<t>, 'txid':<txid>, 'inputs':<list>, 'outputs':<list> }

where `<t>` is the time as an integer or `None`, and each list is `[` +
txos joined by `, ` + `]`, each txo being:

    { 'n': <n>, 'value':<value>, 'address':<address>, 'tx_idx':<idx> }

with `<address>` the raw string (unquoted; an absent address prints as
`None`) and `<idx>` the integer or `None`.  Spacing is exactly as shown:
a space after `'n':` and none after the other colons.

### Rationale

This is the reference's `Transaction.__str__`, spaces and all.  It is
labelled DEBUG but the differential test compares it, so it is normative.

## Result Block

Let the labelled txo `(addr, value)` print as `('<addr>', <value>)`, or
`(None, <value>)` when the address is absent, and a list of them as `[`
+ entries joined by `, ` + `]`.

The block is, in order:

    \nInputs = <list>\n
    \nOutputs = <list>\n
    \nFees = <fee> satoshis\n

then, only with an active hypothesis:

    \nHypothesis: Max intrafees received by a participant = <maker> satoshis\n
    Hypothesis: Max intrafees paid by a participant = <taker> satoshis\n

then:

    \nNb combinations = <nb_cmbn>\n

then, only when `nb_cmbn > 0`:

    Tx entropy = <%f> bits\n

then, only when the efficiency is greater than 0:

    Wallet efficiency = <%f>% (<%f> bits)\n

with the efficiency multiplied by 100 and its `log2` in the parentheses;
then, only when `nb_cmbn > 0`:

    Entropy density = <%f>%\n

Then, by result state (Part 3):

* trivial (`nb_cmbn = 1`, no matrix): nothing more;
* skipped (`nb_cmbn = 0`, no matrix):

      \nSkipped processing of this transaction (too many inputs and/or outputs)\n

* otherwise:

      \nLinkability Matrix (probabilities) :\n
      <matrix of cell / nb_cmbn as binary64>\n

  when `nb_cmbn > 0`, or

      \nLinkability Matrix (#combinations with link) :\n
      <matrix of integer cells>\n

  when `nb_cmbn = 0`; followed by

      \nDeterministic links :\n

  one line per deterministic link (Part 4), in order,

      <input> & <output> are deterministically linked\n

  and

      \nDeterministic link ratio = <%f>%\n

### Requirements

The printer:
  - MUST print `<maker>` and `<taker>` truncated toward zero to integers.
  - MUST print the `Wallet efficiency` line only when the efficiency is
    strictly positive, so never for `nb_cmbn <= 1`.
  - MUST print the entropy density value without multiplying by 100 and
    still follow it with `%`.
  - MUST print the probability matrix over every row of the matrix,
    including the `FEES` row when present, but list deterministic links
    over the printed outputs only.

## Matrix Format

Cells are laid out as numpy 1.x prints a two-dimensional array with
default print options: precision 8, line width 75.

### Cell text, integer matrix

Each cell is the integer in decimal, right-aligned with spaces to the
width of the widest cell.

### Cell text, probability matrix

Let `max` and `min` be the largest and smallest non-zero absolute values
in the matrix.  The matrix is printed in scientific notation when some
value is non-zero and `max >= 1e8`, or `min < 1e-4`, or
`max / min > 1e3`; otherwise in positional notation.

Positional: each value is formatted with 8 digits after the point, the
trailing zeros of the fraction are removed (the point stays: `1.`), then
integer parts are right-aligned with spaces to the widest integer part and
fractions are left-aligned and padded with spaces to the widest fraction.

Scientific: each value is formatted as `d.dddddddde+XX` with 8 mantissa
digits, the trailing zeros of the mantissa are removed, then mantissas
are padded with zeros to the widest fraction, and the exponent is
re-attached as `e` followed by a sign and at least two digits.

### Layout

A row is `[` + cells joined by a single space + `]`.  Rows are joined by
`\n ` and the whole is wrapped in one more pair of brackets, so the first
row starts with `[[`, every other row with ` [`, and the last row ends
with `]]`.  Within a row, when appending the next cell and its separating
space would make the current line longer than 73 characters and the line
already holds a cell, the trailing spaces of the line are removed and the
cell starts a new line indented by two spaces.

### Requirements

The printer:
  - MUST produce, for every matrix, the same text numpy would.  The rules
    above are believed to be complete for the values that occur; where
    they and numpy disagree, numpy is right.

### Rationale

A fixed template would have been simpler, but the reference prints
through numpy and the wrapping and notation switches are visible on
larger transactions.

## Appendix A: Inline Test Vectors

Each vector is the complete output of the command shown, with the
`Duration` line removed.  Amounts are in satoshis.

    $ ludwig --inputs=2,3 --outputs=4,1
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':2, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':3, 'address':b, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':4, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':1, 'address':B, 'tx_idx':None }] }

    Inputs = [('b', 3), ('a', 2)]

    Outputs = [('A', 4), ('B', 1)]

    Fees = 0 satoshis

    Nb combinations = 1
    Tx entropy = 0.000000 bits
    Entropy density = 0.000000%

    Linkability Matrix (probabilities) :
    [[1. 1.]
     [1. 1.]]

    Deterministic links :
    ('b', 3) & ('A', 4) are deterministically linked
    ('a', 2) & ('A', 4) are deterministically linked
    ('b', 3) & ('B', 1) are deterministically linked
    ('a', 2) & ('B', 1) are deterministically linked

    Deterministic link ratio = 100.000000%

    $ ludwig --inputs=49,1 --outputs=49,1
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':49, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':1, 'address':b, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':49, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':1, 'address':B, 'tx_idx':None }] }

    Inputs = [('a', 49), ('b', 1)]

    Outputs = [('A', 49), ('B', 1)]

    Fees = 0 satoshis

    Nb combinations = 2
    Tx entropy = 1.000000 bits
    Wallet efficiency = 66.666667% (-0.584963 bits)
    Entropy density = 0.250000%

    Linkability Matrix (probabilities) :
    [[1.  0.5]
     [0.5 1. ]]

    Deterministic links :
    ('a', 49) & ('A', 49) are deterministically linked
    ('b', 1) & ('B', 1) are deterministically linked

    Deterministic link ratio = 50.000000%

    $ ludwig --inputs=10,10 --outputs=8,2,7,3
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':10, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':10, 'address':b, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':8, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':2, 'address':B, 'tx_idx':None }, { 'n': 2, 'value':7, 'address':C, 'tx_idx':None }, { 'n': 3, 'value':3, 'address':D, 'tx_idx':None }] }

    Inputs = [('a', 10), ('b', 10)]

    Outputs = [('A', 8), ('C', 7), ('D', 3), ('B', 2)]

    Fees = 0 satoshis

    Nb combinations = 3
    Tx entropy = 1.584963 bits
    Wallet efficiency = 42.857143% (-1.222392 bits)
    Entropy density = 0.264160%

    Linkability Matrix (probabilities) :
    [[0.66666667 0.66666667]
     [0.66666667 0.66666667]
     [0.66666667 0.66666667]
     [0.66666667 0.66666667]]

    Deterministic links :

    Deterministic link ratio = 0.000000%

    $ ludwig --inputs=a:5,a:5 --outputs=5,5
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':5, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':5, 'address':a, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':5, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':5, 'address':B, 'tx_idx':None }] }

    Inputs = [('a', 5), ('a', 5)]

    Outputs = [('A', 5), ('B', 5)]

    Fees = 0 satoshis

    Nb combinations = 1
    Tx entropy = 0.000000 bits
    Entropy density = 0.000000%

    Linkability Matrix (probabilities) :
    [[1. 1.]
     [1. 1.]]

    Deterministic links :
    ('a', 5) & ('A', 5) are deterministically linked
    ('a', 5) & ('A', 5) are deterministically linked
    ('a', 5) & ('B', 5) are deterministically linked
    ('a', 5) & ('B', 5) are deterministically linked

    Deterministic link ratio = 100.000000%

    $ ludwig --inputs=a:5,a:5 --outputs=5,5 --options=PRECHECK,LINKABILITY
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':5, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':5, 'address':a, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':5, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':5, 'address':B, 'tx_idx':None }] }

    Inputs = [('a', 5), ('a', 5)]

    Outputs = [('A', 5), ('B', 5)]

    Fees = 0 satoshis

    Nb combinations = 3
    Tx entropy = 1.584963 bits
    Wallet efficiency = 100.000000% (0.000000 bits)
    Entropy density = 0.396241%

    Linkability Matrix (probabilities) :
    [[0.66666667 0.66666667]
     [0.66666667 0.66666667]]

    Deterministic links :

    Deterministic link ratio = 0.000000%

    $ ludwig --inputs=10000000,10000000,5000000 --outputs=9950000,9950000,5000000
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':10000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':10000000, 'address':b, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':c, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':C, 'tx_idx':None }] }

    Inputs = [('a', 10000000), ('b', 10000000), ('c', 5000000)]

    Outputs = [('A', 9950000), ('B', 9950000), ('C', 5000000)]

    Fees = 100000 satoshis

    Nb combinations = 8
    Tx entropy = 3.000000 bits
    Wallet efficiency = 50.000000% (-1.000000 bits)
    Entropy density = 0.500000%

    Linkability Matrix (probabilities) :
    [[0.625 0.625 0.375]
     [0.625 0.625 0.375]
     [0.375 0.375 1.   ]]

    Deterministic links :
    ('c', 5000000) & ('C', 5000000) are deterministically linked

    Deterministic link ratio = 11.111111%

    $ ludwig --inputs=10000000,10000000,5000000 --outputs=9950000,9950000,5000000 --options=PRECHECK
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':10000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':10000000, 'address':b, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':c, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':C, 'tx_idx':None }] }

    Inputs = [('a', 10000000), ('b', 10000000), ('c', 5000000)]

    Outputs = [('A', 9950000), ('B', 9950000), ('C', 5000000)]

    Fees = 100000 satoshis

    Nb combinations = 0

    Linkability Matrix (#combinations with link) :
    [[0 0 0]
     [0 0 0]
     [0 0 1]]

    Deterministic links :

    Deterministic link ratio = 0.000000%

    $ ludwig --inputs=10000000,10000000,5000000 --outputs=9950000,9950000,5000000 --options=PRECHECK,LINKABILITY,MERGE_FEES
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':10000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':10000000, 'address':b, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':c, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':C, 'tx_idx':None }] }

    Inputs = [('a', 10000000), ('b', 10000000), ('c', 5000000)]

    Outputs = [('A', 9950000), ('B', 9950000), ('C', 5000000)]

    Fees = 100000 satoshis

    Nb combinations = 2
    Tx entropy = 1.000000 bits
    Wallet efficiency = 12.500000% (-3.000000 bits)
    Entropy density = 0.166667%

    Linkability Matrix (probabilities) :
    [[1.  1.  0.5]
     [1.  1.  0.5]
     [0.5 0.5 1. ]
     [1.  1.  0.5]]

    Deterministic links :
    ('a', 10000000) & ('A', 9950000) are deterministically linked
    ('b', 10000000) & ('A', 9950000) are deterministically linked
    ('a', 10000000) & ('B', 9950000) are deterministically linked
    ('b', 10000000) & ('B', 9950000) are deterministically linked
    ('c', 5000000) & ('C', 5000000) are deterministically linked

    Deterministic link ratio = 55.555556%

    $ ludwig --inputs=10000000,10000000,5000000 --outputs=9950000,9950000,5000000 --maxnbtxos=2
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':10000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':10000000, 'address':b, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':c, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }, { 'n': 2, 'value':5000000, 'address':C, 'tx_idx':None }] }

    Inputs = [('a', 10000000), ('b', 10000000), ('c', 5000000)]

    Outputs = [('A', 9950000), ('B', 9950000), ('C', 5000000)]

    Fees = 100000 satoshis

    Nb combinations = 0

    Skipped processing of this transaction (too many inputs and/or outputs)

    $ ludwig --inputs=10000000,10000000 --outputs=9950000,9950000 --cjmaxfeeratio=0.005
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':10000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':10000000, 'address':b, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }] }

    Inputs = [('a', 10000000), ('b', 10000000)]

    Outputs = [('A', 9950000), ('B', 9950000)]

    Fees = 100000 satoshis

    Hypothesis: Max intrafees received by a participant = 49750 satoshis
    Hypothesis: Max intrafees paid by a participant = 49750 satoshis

    Nb combinations = 3
    Tx entropy = 1.584963 bits
    Wallet efficiency = 100.000000% (0.000000 bits)
    Entropy density = 0.396241%

    Linkability Matrix (probabilities) :
    [[0.66666667 0.66666667]
     [0.66666667 0.66666667]]

    Deterministic links :

    Deterministic link ratio = 0.000000%

    $ ludwig --inputs=6000000,4000000,10000000 --outputs=9950000,9950000 --options=LINKABILITY
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':6000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':4000000, 'address':b, 'tx_idx':None }, { 'n': 2, 'value':10000000, 'address':c, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }] }

    Inputs = [('c', 10000000), ('a', 6000000), ('b', 4000000)]

    Outputs = [('A', 9950000), ('B', 9950000)]

    Fees = 100000 satoshis

    Nb combinations = 3
    Tx entropy = 1.584963 bits
    Wallet efficiency = 42.857143% (-1.222392 bits)
    Entropy density = 0.316993%

    Linkability Matrix (probabilities) :
    [[0.66666667 0.66666667 0.66666667]
     [0.66666667 0.66666667 0.66666667]]

    Deterministic links :

    Deterministic link ratio = 0.000000%

    $ ludwig --inputs=7 --outputs=3,4
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':7, 'address':a, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':3, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':4, 'address':B, 'tx_idx':None }] }

    Inputs = [('a', 7)]

    Outputs = [('A', 3), ('B', 4)]

    Fees = 0 satoshis

    Nb combinations = 1
    Tx entropy = 0.000000 bits
    Entropy density = 0.000000%

    $ ludwig --inputs=a:6000000,a:4000000,10000000 --outputs=9950000,9950000 --maxnbtxos=1
    DEBUG: Using inline transaction


    --- inline -------------------------------------
    DEBUG: Tx fetched: { 'height': -1, 'time':None, 'txid':inline, 'inputs':[{ 'n': 0, 'value':6000000, 'address':a, 'tx_idx':None }, { 'n': 1, 'value':4000000, 'address':a, 'tx_idx':None }, { 'n': 2, 'value':10000000, 'address':c, 'tx_idx':None }], 'outputs':[{ 'n': 0, 'value':9950000, 'address':A, 'tx_idx':None }, { 'n': 1, 'value':9950000, 'address':B, 'tx_idx':None }] }

    Inputs = [('c', 10000000), ('a', 6000000), ('a', 4000000)]

    Outputs = [('A', 9950000), ('B', 9950000)]

    Fees = 100000 satoshis

    Nb combinations = 0

    Skipped processing of this transaction (too many inputs and/or outputs)

## Appendix B: On-Chain Test Vectors

These transactions exercise one feature each.  `E` is the entropy in
bits.  Fetch them with any provider; the figures do not depend on the
source.

| txid | shape | expected |
|---|---|---|
| `4aff3b06e3e2838240c22721f381a186cbc92e84ea0a04228fdec4950efaa524` | 1 in, 1 out | trivial: 1 combination, E = 0 |
| `8c5feb901f3983b0f28d996f9606d895d75136dbe8d77ed1d6c7340a403a73bf` | 2 in, 2 out | 2 combinations, E = 1; an input address reused as an output |
| `8e56317360a548e8ef28ec475878ef70d1371bee3526c017ac22ad61ae5740b8` | 2 in, 4 out | 3 combinations, E = 1.58, efficiency 42.9% |
| `812bee538bd24d03af7876a77c989b2c236c063a5803c720769fc55222d36b47` | 2 in, 4 out | same matrix as the previous one |
| `323df21f0b0756f98336437aa3d2fb87e02b59f1946b714a7b09df04d429dec2` | 5 in, 5 out | 1496 combinations, E = 10.55, efficiency 100%, no deterministic link |
| `a9b5563592099bf6ed68e7696eeac05c8cb514e21490643e0b7a9b72dac90b07` | 9 in, 5 out | 1 combination, every pair deterministically linked |
| `c19c342d9e9c3f97f1e444d74f58878a7135259239227205906bd6153c547235` | 3 in, 2 out | E = 2 without `MERGE_INPUTS`, E = 0 with it |
| `7d588d52d1cece7a18d663c977d6143016b5b326404bbf286bc024d5d54fcecb` | 5 in, 7 out | E = 0 by default, 95 combinations with `cj_max_fee_ratio = 0.005` |
| `97070d6c452259a63ee669545ba0ecd1eef5fbd11ab4600e058d596fce9f8502` | 99 in, 2 out | skipped: over `max_txos = 12` |
| `24a3cc7abbc79689c2969bf0d14ab6fc83482e43d12e7a267823231764ce14b5` | 19 in, 2 out | not skipped: all inputs share one address and pack into one |
