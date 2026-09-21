#!/bin/sh
# Differential test: the C and Python implementations must print the same
# thing for the same transaction (the timing line excepted).
#
# usage: tests/compare_with_python.sh [COUNT] [SEED] [big]
#
# Needs a python that can import the reference implementation
# (https://github.com/Samourai-Wallet/boltzmann) and its dependencies (numpy,
# sympy, sortedcontainers): either pip install it, or set BOLTZMANN_PY to a
# clone of it.  Set PYTHON to choose the interpreter (default: python3).
set -e
cd "$(dirname "$0")/.."
COUNT=${1:-60}
SEED=${2:-1}
BIG=${3:-}
PYTHON=${PYTHON:-python3}
PY=$PYTHON
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$PY" tests/gen_tx.py "$SEED" "$WORK/txs" "$COUNT" $BIG

fail=0
total=0
for f in "$WORK"/txs/*.json; do
	for opts in \
		"PRECHECK,LINKABILITY,MERGE_INPUTS 600 12 0" \
		"LINKABILITY 600 12 0" \
		"PRECHECK 600 12 0" \
		"PRECHECK,LINKABILITY,MERGE_INPUTS,MERGE_FEES 600 12 0" \
		"PRECHECK,LINKABILITY,MERGE_INPUTS 600 3 0" \
		"PRECHECK,LINKABILITY,MERGE_INPUTS 600 12 0.005" \
		"PRECHECK,LINKABILITY,MERGE_INPUTS 0 12 0"; do
		set -- $opts
		total=$((total + 1))
		"$PY" tests/python_ludwig_file.py "$f" "$1" "$2" "$3" "$4" \
			| grep -v '^Duration = ' > "$WORK/py.out"
		./ludwig --file="$f" --options="$1" --duration="$2" --maxnbtxos="$3" --cjmaxfeeratio="$4" \
			| grep -v '^Duration = ' > "$WORK/c.out"
		if ! diff -u "$WORK/py.out" "$WORK/c.out" > "$WORK/diff.out"; then
			fail=$((fail + 1))
			echo "MISMATCH: $f options=$1 maxtxos=$3 cjratio=$4"
			head -40 "$WORK/diff.out"
		fi
	done
done
echo "$((total - fail))/$total runs identical"
[ "$fail" -eq 0 ]
