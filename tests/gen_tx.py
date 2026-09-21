'''
Generates random transactions in blockchain.info JSON format for the
differential test.  The shapes are chosen to exercise the interesting
paths: equal outputs (coinjoins), repeated input addresses (MERGE_INPUTS),
zero-value outputs, single input / single output, and plain payments.

usage: gen_tx.py SEED OUTDIR COUNT [big]
'''
import json
import os
import random
import sys

seed, outdir, count = int(sys.argv[1]), sys.argv[2], int(sys.argv[3])
big = len(sys.argv) > 4 and sys.argv[4] == 'big'   # larger shapes: wrapping, sci notation
rng = random.Random(seed)
os.makedirs(outdir, exist_ok=True)

def addr(i):
    return 'addr%d' % i

def coinjoin(n_in, n_out, fee):
    # n_in participants each pay `amount` + change; n_out equal outputs.
    amount = rng.choice([100000, 1000000, 5000000, 25000000])
    ins, outs = [], []
    for i in range(n_in):
        extra = rng.randint(0, 3000000)
        ins.append((addr(i), amount + extra + fee // n_in + (fee % n_in if i == 0 else 0)))
        if extra > 0 and len(outs) < n_out - n_in:
            outs.append((addr(100 + i), extra))
    for i in range(n_in):
        outs.append((addr(200 + i), amount))
    rng.shuffle(outs)
    return ins, outs[:max(n_out, 2)]

def payment(n_in, n_out, fee):
    ins = [(addr(rng.randint(0, 3)), rng.randint(1000, 5000000)) for _ in range(n_in)]
    total = sum(v for _, v in ins) - fee
    outs = []
    for i in range(n_out - 1):
        v = rng.randint(1, max(1, total // n_out))
        outs.append((addr(100 + i), v))
        total -= v
    outs.append((addr(300), total))
    return ins, outs

def stonewall(fee):
    # 2 parties, 4 equal outputs + 2 change outputs.
    amount = rng.choice([100000, 500000])
    ins = [(addr(0), amount * 2 + 70000), (addr(0), 30000),
           (addr(1), amount * 2 + 80000)]
    outs = [(addr(200), amount)] * 4 + [(addr(101), 100000 - fee), (addr(102), 80000)]
    rng.shuffle(outs)
    return ins, outs

for k in range(count):
    fee = rng.choice([0, 1000, 12345])
    kind = rng.choice(['cj', 'cj', 'pay', 'pay', 'stonewall', 'edge'])
    if kind == 'cj':
        ins, outs = coinjoin(rng.randint(2, 8 if big else 5), rng.randint(2, 12 if big else 8), fee)
    elif kind == 'pay':
        ins, outs = payment(rng.randint(1, 7 if big else 5), rng.randint(1, 9 if big else 5), fee)
    elif kind == 'stonewall':
        ins, outs = stonewall(fee)
    else:
        ins, outs = payment(rng.randint(2, 4), rng.randint(2, 4), fee)
        outs.append((addr(999), 0))   # OP_RETURN-like output
    # Occasionally make the fee negative-proof: ensure sum(ins) >= sum(outs)
    diff = sum(v for _, v in ins) - sum(v for _, v in outs)
    if diff < 0:
        ins[0] = (ins[0][0], ins[0][1] - diff)
    tx = {
        'hash': 'tx%08d' % k,
        'block_height': 500000 + k,
        'time': 1500000000 + k,
        'inputs': [{'prev_out': {'n': i, 'value': v, 'addr': a, 'tx_index': 1000 + i}}
                   for i, (a, v) in enumerate(ins)],
        'out': [{'n': i, 'value': v, 'addr': a, 'tx_index': 2000 + k} for i, (a, v) in enumerate(outs)],
    }
    with open(os.path.join(outdir, 'tx%04d.json' % k), 'w') as f:
        json.dump(tx, f)
