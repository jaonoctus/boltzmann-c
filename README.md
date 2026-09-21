# Boltzmann in C

A C port of [Boltzmann](https://github.com/Samourai-Wallet/boltzmann), the Bitcoin transaction entropy and
linkability analyser, written to be read. The output is byte-for-byte the
output of the Python implementation (timing line excepted), and the code
follows the [Linux kernel coding style](https://www.kernel.org/doc/html/v4.10/process/coding-style.html).

## Building

Requires a C11 compiler, libcurl and libm. No other dependencies.

```
make            # builds ./ludwig
make check      # unit tests (tests/run-*.c)
make compare    # differential test against the Python implementation
```

`make compare` needs the Python reference implementation and its
dependencies (numpy, sympy, sortedcontainers). Either install it into the
interpreter you use (`pip install git+https://github.com/Samourai-Wallet/boltzmann`)
or point `BOLTZMANN_PY=...` at a clone of it. The interpreter defaults to
`python3`; set `PYTHON=...` to override.

## Docker

A prebuilt image is on Docker Hub as `jaonoctus/ludwig` for linux/amd64,
linux/arm64 and linux/arm/v7:

```
docker run --rm jaonoctus/ludwig --txids=<txid>
docker run --rm -v "$PWD:/data" jaonoctus/ludwig --file=/data/tx.json
curl -s https://mempool.space/api/tx/<txid> | docker run --rm -i jaonoctus/ludwig --file=-
```

To build it yourself:

```
docker build -t ludwig .
docker buildx build --platform linux/amd64,linux/arm64,linux/arm/v7 -t jaonoctus/ludwig --push .
```

`docker compose build` and `docker compose run --rm ludwig <args>` do the
same; compose mounts the current directory at `/data` and forwards the
`BOLTZMANN_RPC_*` variables for `--rpc`. The image is Alpine with libcurl
and CA certificates, about 23 MB. The build stage also runs `make check`.

## Usage

Same flags as the reference's `ludwig.py`, plus `--mempool`, `--file` and
`--inputs`/`--outputs`:

```
./ludwig --txids=8e56317360a548e8ef28ec475878ef70d1371bee3526c017ac22ad61ae5740b8
./ludwig --mempool --txids=<txid>,<txid>
./ludwig --blockstream --testnet --txids=<txid>
./ludwig --rpc --txids=<txid>          # BOLTZMANN_RPC_{USERNAME,PASSWORD,HOST,PORT}
./ludwig --file=tx.json                 # blockchain.info or Esplora JSON, offline
curl -s https://mempool.space/api/tx/<txid> | ./ludwig --file=-    # same, from a pipe
./ludwig --inputs=2,3 --outputs=4,1     # a made-up transaction, nothing fetched
./ludwig --options=PRECHECK,LINKABILITY,MERGE_INPUTS --maxnbtxos=12 --duration=600 --cjmaxfeeratio=0.005 --txids=...
```

A made-up transaction can be piped in the same way. Only the values and
addresses matter; the other fields are there because the blockchain.info
parser expects them. Inputs 2 and 3 paying outputs 4 and 1:

```
echo '{"hash":"2341","block_height":1,"time":1,"inputs":[{"prev_out":{"n":0,"value":2,"addr":"a","tx_index":1}},{"prev_out":{"n":1,"value":3,"addr":"b","tx_index":2}}],"out":[{"n":0,"value":4,"addr":"A","tx_index":3},{"n":1,"value":1,"addr":"B","tx_index":3}]}' | ./ludwig --file=-
```

Neither input can pay either output on its own, so there is a single
combination, 0 bits of entropy, and every input is deterministically linked
to every output.

`--inputs` and `--outputs` say the same thing without the JSON. Amounts are
satoshis, comma-separated, each optionally `LABEL:AMOUNT`; unlabelled
inputs are called a, b, c... and outputs A, B, C... Repeating a label is
address reuse, which `MERGE_INPUTS` then packs:

```
./ludwig --inputs=2,3 --outputs=4,1              # 1 combination, 0 bits
./ludwig --inputs=49,1 --outputs=49,1            # 2 combinations, 1 bit
./ludwig --inputs=10,10 --outputs=8,2,7,3        # 3 combinations, 1.58 bits
./ludwig --inputs=a:5,a:5 --outputs=5,5          # 1 combination: same address, merged
./ludwig --inputs=a:5,a:5 --outputs=5,5 --options=PRECHECK,LINKABILITY   # 3 combinations
```

Data sources: blockchain.info (default), Blockstream (`-b`), mempool.space
(`-m`), a local bitcoind (`-p`, needs `txindex=1`), a JSON file (`-f`), or
amounts given inline (`--inputs`/`--outputs`).
Smartbit is gone, so `--smartbit` now says so and exits.

Run `./ludwig --help` for the full option list.

## Example transactions

Each of these exercises one feature. `E` is the entropy in bits.

| txid | Shape | What it shows |
|---|---|---|
| `4aff3b06e3e2838240c22721f381a186cbc92e84ea0a04228fdec4950efaa524` | 1 in, 1 out | The trivial case: 1 combination, E = 0. |
| `8c5feb901f3983b0f28d996f9606d895d75136dbe8d77ed1d6c7340a403a73bf` | 2 in, 2 out | Two interpretations, E = 1. One input address is reused as an output. |
| `8e56317360a548e8ef28ec475878ef70d1371bee3526c017ac22ad61ae5740b8` | 2 in, 4 out | DarkWallet coinjoin: 3 combinations, E = 1.58, efficiency 42.9%. |
| `812bee538bd24d03af7876a77c989b2c236c063a5803c720769fc55222d36b47` | 2 in, 4 out | The next DarkWallet coinjoin; same matrix as the one above. |
| `323df21f0b0756f98336437aa3d2fb87e02b59f1946b714a7b09df04d429dec2` | 5 in, 5 out | Samourai Whirlpool, 0.05 BTC pool: 1496 combinations, E = 10.55, efficiency 100%, no deterministic links. |
| `a9b5563592099bf6ed68e7696eeac05c8cb514e21490643e0b7a9b72dac90b07` | 9 in, 5 out | Five equal outputs but not a coinjoin: 1 combination and a long deterministic links list. |
| `c19c342d9e9c3f97f1e444d74f58878a7135259239227205906bd6153c547235` | 3 in, 2 out | Three inputs from one address. E = 2 by default, E = 0 with `MERGE_INPUTS`. |
| `7d588d52d1cece7a18d663c977d6143016b5b326404bbf286bc024d5d54fcecb` | 5 in, 7 out | Four-party coinjoin where makers are paid: E = 0 by default, 95 combinations with `--cjmaxfeeratio=0.005`. |
| `97070d6c452259a63ee669545ba0ecd1eef5fbd11ab4600e058d596fce9f8502` | 99 in, 2 out | Over the default `--maxnbtxos=12`: processing is skipped. |
| `24a3cc7abbc79689c2969bf0d14ab6fc83482e43d12e7a267823231764ce14b5` | 19 in, 2 out | Not skipped: all 19 inputs share one address, so `MERGE_INPUTS` packs them into one entity first. |

```
./ludwig --mempool --txids=8c5feb901f3983b0f28d996f9606d895d75136dbe8d77ed1d6c7340a403a73bf
./ludwig --mempool --options=PRECHECK,LINKABILITY,MERGE_INPUTS --txids=c19c342d9e9c3f97f1e444d74f58878a7135259239227205906bd6153c547235
./ludwig --mempool --cjmaxfeeratio=0.005 --txids=7d588d52d1cece7a18d663c977d6143016b5b326404bbf286bc024d5d54fcecb
```

The first six come from LaurentMT's
[Bitcoin Transactions & Privacy](https://gist.github.com/LaurentMT/e758767ca4038ac40aaf)
series and the OXT Research articles; the rest were picked from the chain
to show a feature.

## Layout

```
ludwig.c                   command line: parse flags, pick a provider, loop over txids
boltzmann/
  transaction.[ch]         the transaction model every provider produces
  tx_processor.[ch]        around the linker: filtering, merging by address,
                           coinjoin detection, efficiency score, id -> address
  txos_linker.[ch]         the algorithm: combinations and the linkability matrix
  perfect_cj.[ch]          perfect-coinjoin combination counts (the efficiency yardstick)
  perfect_cj_table.h       generated from the Python constants
  display.[ch]             prints the results exactly like ludwig.py
providers/
  provider.h               the provider interface + the two JSON translators
  blockchain_info.c        https://blockchain.info/rawtx/<txid>
  esplora.c                Blockstream and mempool.space (same API)
  bitcoind_rpc.c           getrawtransaction, one extra lookup per input
  file.c                   a JSON file in either of the two formats, or stdin
  inline.c                 --inputs/--outputs: a made-up transaction
common/
  tal.[ch]                 hierarchical allocator (subset of ccan/tal)
  u64map.[ch]              small hash map, used for the link counters
  pyfmt.[ch]               Python str(float) and numpy array printing
  json.[ch]                minimal JSON parser
  http.[ch]                libcurl wrapper
  utils.[ch], types.h      die(), stable sort, clock, short integer types
tests/
  run-linker.c             hand-checked transactions through the linker
  run-pyfmt.c              formatter cases recorded from CPython/numpy
  compare_with_python.sh   random transactions, every option, diff vs Python
  compare_numpy_print.sh   random matrices, diff vs numpy
  gen_tx.py, python_ludwig_file.py   helpers for the above
```

## How the analysis works

Read `boltzmann/txos_linker.c` top to bottom; it is laid out in the order
the analysis runs. The short version:

1. **Prepare.** Inputs and outputs are sorted by decreasing amount. Every
   subset of inputs (an *aggregate*) is a bitmask, and the sum of every
   aggregate is tabulated: 2^n values for n txos. Same for outputs.

2. **Match.** An input aggregate *matches* an output aggregate when the
   input sum covers the output sum and the difference is at most the fee
   (with `--cjmaxfeeratio`, participants may also pay each other, which
   widens the tolerance). For every input value that matches anything, the
   list of output aggregates it can pay is recorded.

3. **Precheck** (`PRECHECK`, Coinjoin Sudoku). Count, for each (input,
   output) pair, how many matching aggregate pairs contain both. A pair
   present in every match involving that input is a *deterministic link*:
   true whatever the real story is. Inputs deterministically tied to the
   same output are then *packed* into one synthetic input, which shrinks
   the search below.

4. **Search** (`LINKABILITY`). A depth-first search decomposes the full
   input set by splitting off one matched aggregate at a time, in
   increasing order so each partition is visited once. Each step keeps only
   the output-side decompositions consistent with the input side. When a
   branch is exhausted its counts are folded upwards: the total number of
   valid *combinations* (consistent partitions of the whole transaction),
   and, for each (input aggregate, output aggregate) pair, how many
   combinations it appears in. The (input, output) matrix is the sum of
   these over the aggregates containing them.

5. **Unpack and report.** Packs are expanded back (a pack's matrix column
   is copied once per member), ids become addresses, and
   `display_results()` prints:

   - `Nb combinations` and `Tx entropy` = log2 of it;
   - `Wallet efficiency` = combinations divided by those of the nearest
     *perfect coinjoin* of the same shape (`perfect_cj.c`);
   - `Entropy density` = entropy / number of txos (printed with a `%`
     sign by the reference, and therefore here too, but it is bits per txo);
   - the linkability matrix as probabilities (rows = outputs, columns =
     inputs, in the printed order), the deterministic links, and their
     ratio over all pairs.

`tx_processor.c` handles the pieces around this: dropping zero-value
outputs, grouping inputs by address for `MERGE_INPUTS`, detecting the
coinjoin pattern used by the intrafees heuristic, and the efficiency score.

## Faithfulness notes

Where the reference has a quirk the analysis output depends on, the port
reproduces it and says so in a comment:

- `MERGE_OUTPUTS` is accepted but changes nothing: the reference only ever
  packs inputs. `MERGE_FEES` leaves the synthetic fee output out of the
  printed `Outputs` list but in the matrix, so the matrix has one more row.
- Sorting is stable, ties in amount keep their order, and packs sort by
  their summed amount; this fixes the row/column order of the matrix.
- The Python prints `%i` of the intrafees floats (truncation), the
  `Entropy density` percent sign, and numpy's array layout (8 decimals,
  75-column wrapping, scientific notation when values span more than three
  orders of magnitude). `common/pyfmt.c` implements those rules and
  `tests/compare_numpy_print.sh` checks them against numpy.

Deliberate differences, all outside the analysis itself:

- Esplora outputs without an address (OP_RETURN) fall back to the script
  hex instead of raising, and coinbase inputs from any provider become an
  "unknown" txo instead of crashing. Zero-value txos never take part in the
  analysis, so results are unchanged.
- The bitcoind provider gets the block height from `getblockheader`
  rather than the reference's `gettxout` confirmation trick, accepts the
  modern `address` field as well as the old `addresses` list, and
  converts BTC amounts from the JSON text rather than through a float.
- Perfect-coinjoin counts outside the precomputed table are computed by
  enumerating integer partitions in double precision; the reference either
  uses sympy or raises a `KeyError` (for `n_out = 60`).
- The `Duration` line is real timing and is the only line excluded from
  the differential test. Fetch errors print the message without a Python
  traceback.
- Combination counts are 64-bit. With the default `--maxnbtxos=12` the
  largest possible count is about 1.5e13; raising the limit far beyond that
  would need bignums (and a lot of patience: the search is exponential).

## Testing

```
make check                             # unit tests
make compare                           # 60 random txs x 7 option sets vs Python
BOLTZMANN_PY=~/src/boltzmann make compare  # same, reference taken from a clone
tests/compare_with_python.sh 40 3 big  # larger shapes (wrapping, sci notation)
tests/compare_numpy_print.sh 400       # matrix printer vs numpy
```
