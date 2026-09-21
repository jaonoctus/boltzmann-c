#!/bin/sh
# Differential test of the matrix printer against numpy on random matrices,
# including shapes that wrap and values that force scientific notation.
#
# usage: tests/compare_numpy_print.sh [COUNT]
#
# Needs a python with numpy; set PYTHON to choose it (default: python3).
set -e
cd "$(dirname "$0")/.."
COUNT=${1:-300}
PYTHON=${PYTHON:-python3}
make -s tests/run-pyfmt
"$PYTHON" - "$COUNT" tests/run-pyfmt <<'PY'
import random, subprocess, sys
import numpy as np
count, prog = int(sys.argv[1]), sys.argv[2]
rng = random.Random(7)
fail = 0
for k in range(count):
    rows, cols = rng.randint(1, 13), rng.randint(1, 13)
    kind = rng.choice(['probs', 'probs', 'ratios', 'tiny', 'exact'])
    if kind == 'probs':
        nb = rng.randint(1, 5000)
        vals = [rng.randint(0, nb) / nb for _ in range(rows * cols)]
    elif kind == 'ratios':
        nb = rng.choice([3, 7, 12, 512, 1024, 3000])
        vals = [rng.randint(0, nb) / nb for _ in range(rows * cols)]
    elif kind == 'tiny':
        vals = [rng.choice([0.0, 1e-5, 1e-9, 0.5, 1.0, 1 / 3]) for _ in range(rows * cols)]
    else:
        vals = [rng.choice([0.0, 0.5, 0.25, 1.0, 0.125]) for _ in range(rows * cols)]
    want = str(np.array(vals).reshape(rows, cols))
    inp = "%d %d %s\n" % (rows, cols, " ".join(repr(v) for v in vals))
    got = subprocess.run([prog, "--matrix"], input=inp, capture_output=True, text=True).stdout.rstrip("\n")
    if got != want:
        fail += 1
        print("MISMATCH %dx%d %s\n--- numpy\n%s\n--- c\n%s" % (rows, cols, kind, want, got))
print("%d/%d matrices identical" % (count - fail, count))
sys.exit(1 if fail else 0)
PY
