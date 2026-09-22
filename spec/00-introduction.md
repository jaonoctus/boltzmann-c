# Part 0: Introduction and Index

This directory specifies the Boltzmann analysis of a Bitcoin transaction as
performed by `ludwig`: given the amounts of a transaction's inputs and
outputs, count every consistent story of who paid whom, and derive from that
count how much a chain observer can learn.

The documents describe observable behaviour.  Two implementations that
follow them, given the same transaction and the same parameters, MUST print
byte-identical output, the `Duration` line excepted.  Where an internal step
is described in order, it is because a different order would change the
output.  Anything not observable in the output is left to the implementer.

The Python implementation at
https://github.com/Samourai-Wallet/boltzmann is the reference.  Where this
specification and the reference disagree, the reference is right and the
specification has a bug, with the exceptions listed in Part 1 under
"Differences from the reference".

## Conventions

The key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are to be
interpreted as described in RFC 2119.

Three roles appear in the requirements:

* the *provider*: turns a txid, a file or inline amounts into the
  transaction model of Part 1;
* the *analyser*: runs Parts 2 to 5 on that model;
* the *printer*: writes the text of Part 6.

`ludwig` plays all three.  The split only serves to say who a requirement
binds.

Amounts are integers in satoshis unless stated otherwise.  `log2` is the
binary logarithm.  `|X|` is the number of elements of X.

## Index

1. [Part 1](01-transaction.md): The Transaction Model
2. [Part 2](02-preprocessing.md): Parameters, Filtering, Packing and the
   Order of Operations
3. [Part 3](03-linker.md): Aggregates, Matching, Deterministic Links and
   Combinations
4. [Part 4](04-metrics.md): Entropy, Efficiency and the Deterministic Link
   Ratio
5. [Part 5](05-intrafees.md): The Coinjoin Intrafees Hypothesis
6. [Part 6](06-output.md): Textual Output Format and Test Vectors

## Glossary

* *txo*: one input (a previous output being spent) or one output of the
  analysed transaction, together with its amount in satoshis.
* *identifier*: the name a txo carries through the analysis, `I<n>` for
  the input at position `n` of the transaction and `O<n>` for the output
  at position `n`.  Positions are zero-based.
* *filtered txo*: a txo with a positive amount.  Only filtered txos take
  part in the analysis.
* *working list*: the list of inputs (or of outputs) as the analyser
  currently holds it: filtered, possibly packed, possibly sorted.
* *aggregate*: a subset of the working inputs (or of the working outputs),
  and by extension the sum of their amounts.
* *matched pair*: an input aggregate and an output aggregate whose sums
  balance under the matching rule of Part 3.
* *combination*: a partition of all working inputs and all working outputs
  into matched pairs.
* *link*: an input and an output that belong to the same matched pair of a
  given combination.
* *deterministic link*: a link present in every combination.
* *linkability matrix*: rows are outputs, columns are inputs, and each cell
  counts the combinations in which that input and output are linked.
* *entity*: one or more txos assumed to be controlled by the same party.
* *pack*: several inputs known to form one entity, replaced by a single
  synthetic input during the search and expanded again in the result.
* *synthetic txo*: a txo that does not exist in the transaction: a pack
  (`PACK_I<k>`) or the fee output (`FEES`).
* *hypothesis*: the intrafees tolerance of Part 5, when active.
* *perfect coinjoin*: a transaction with equal inputs, equal outputs, no
  fee, and one count dividing the other; the yardstick for efficiency.
