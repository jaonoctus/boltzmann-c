'''
Runs the Python reference implementation (Boltzmann) on a transaction stored in a JSON
file (blockchain.info format), printing exactly what ludwig.py prints.
Used by compare_with_python.sh to diff against the C implementation.

usage: python_ludwig_file.py FILE [OPTIONS] [MAXDURATION] [MAXTXOS] [CJRATIO]
'''
import json
import os
import sys

# The reference lives at https://github.com/Samourai-Wallet/boltzmann.
# Import it from an installed package, or from a clone named by BOLTZMANN_PY.
if os.environ.get('BOLTZMANN_PY'):
    sys.path.insert(0, os.environ['BOLTZMANN_PY'])
try:
    from boltzmann.utils.transaction import Transaction
    from boltzmann.utils.tx_processor import process_tx
    from boltzmann.ludwig import display_results
except ImportError as e:
    sys.exit('cannot import the Python reference implementation (%s).\n'
             'pip install git+https://github.com/Samourai-Wallet/boltzmann or set BOLTZMANN_PY to a clone of it.' % e)

path = sys.argv[1]
options = sys.argv[2].split(',') if len(sys.argv) > 2 else ['PRECHECK', 'LINKABILITY', 'MERGE_INPUTS']
max_duration = float(sys.argv[3]) if len(sys.argv) > 3 else 600
max_txos = int(sys.argv[4]) if len(sys.argv) > 4 else 12
cj_ratio = float(sys.argv[5]) if len(sys.argv) > 5 else 0

with open(path) as f:
    tx = Transaction(json.load(f))

print("DEBUG: Using local file")
print('\n\n--- %s -------------------------------------' % tx.txid)
print("DEBUG: Tx fetched: {0}".format(str(tx)))
results = process_tx(tx, options, max_duration, max_txos, cj_ratio)
display_results(*results)
