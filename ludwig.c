/*
 * ludwig: the Boltzmann command line.
 *
 * Fetches each requested transaction from the chosen data source, runs
 * the analysis and prints the results.  Named after Ludwig Boltzmann, as
 * in the Python reference implementation.
 */
#include "boltzmann/display.h"
#include "boltzmann/tx_processor.h"
#include "common/tal.h"
#include "common/utils.h"
#include "providers/provider.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
	printf("ludwig [--rpc] [--testnet] [--blockstream] [--mempool] [--file=PATH]\n"
	       "       [--inputs=AMOUNTS --outputs=AMOUNTS]\n"
	       "       [--duration=600] [--maxnbtxos=12] [--cjmaxfeeratio=0]\n"
	       "       [--options=PRECHECK,LINKABILITY,MERGE_FEES,MERGE_INPUTS,MERGE_OUTPUTS]\n"
	       "       [--txids=8e56317360a548e8ef28ec475878ef70d1371bee3526c017ac22ad61ae5740b8,...]\n"
	       "\n"
	       "[-t OR --txids] = List of txids to be processed.\n"
	       "\n"
	       "[-p OR --rpc] = Use bitcoind's RPC interface as source of blockchain data\n"
	       "                (BOLTZMANN_RPC_USERNAME/PASSWORD/HOST/PORT environment variables)\n"
	       "\n"
	       "[-T OR --testnet] = Use testnet as source of blockchain data\n"
	       "\n"
	       "[-b OR --blockstream] = Use Blockstream as source of blockchain data\n"
	       "\n"
	       "[-m OR --mempool] = Use mempool.space as source of blockchain data\n"
	       "\n"
	       "[-f OR --file] = Read the transaction from a local JSON file\n"
	       "                 (blockchain.info or Esplora format); \"-\" reads stdin\n"
	       "\n"
	       "[--inputs=AMOUNTS --outputs=AMOUNTS] = Analyse a made-up transaction given inline.\n"
	       "                 Comma-separated amounts in satoshis, each optionally LABEL:AMOUNT;\n"
	       "                 e.g. --inputs=2,3 --outputs=4,1 or --inputs=a:5,a:5 --outputs=8,2.\n"
	       "                 Unlabelled inputs are a, b, c... and outputs A, B, C...\n"
	       "\n"
	       "[-d OR --duration] = Maximum number of seconds allocated to the processing of a single transaction. Default value is 600\n"
	       "\n"
	       "[-x OR --maxnbtxos] = Maximum number of inputs or outputs. Transactions with more than maxnbtxos inputs or outputs are not processed. Default value is 12.\n"
	       "\n"
	       "[-r OR --cjmaxfeeratio] = Max intrafees paid by the taker of a coinjoined transaction. Expressed as a percentage of the coinjoined amount. Default value is 0.\n"
	       "\n"
	       "[-o OR --options] = Options to be applied during processing. Default value is PRECHECK, LINKABILITY, MERGE_INPUTS\n"
	       "    Available options are :\n"
	       "    PRECHECK = Checks if deterministic links exist without processing the entropy of the transaction. Similar to Coinjoin Sudoku by K.Atlas.\n"
	       "    LINKABILITY = Computes the entropy of the transaction and the txos linkability matrix.\n"
	       "    MERGE_INPUTS = Merges inputs \"controlled\" by a same address. Speeds up computations.\n"
	       "    MERGE_OUTPUTS = Merges outputs \"controlled\" by a same address. Speeds up computations but this option is not recommended.\n"
	       "    MERGE_FEES = Processes fees as an additional output paid by a single participant. May speed up computations.\n");
}

/* "PRECHECK,LINKABILITY,..." to a bitmask. */
static u32 parse_options(const char *list)
{
	u32 options = 0;
	char *copy = tal_strdup(NULL, list), *tok, *save = NULL;

	for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		while (*tok == ' ')
			tok++;
		if (streq(tok, "PRECHECK"))
			options |= OPT_PRECHECK;
		else if (streq(tok, "LINKABILITY"))
			options |= OPT_LINKABILITY;
		else if (streq(tok, "MERGE_FEES"))
			options |= OPT_MERGE_FEES;
		else if (streq(tok, "MERGE_INPUTS"))
			options |= OPT_MERGE_INPUTS;
		else if (streq(tok, "MERGE_OUTPUTS"))
			options |= OPT_MERGE_OUTPUTS;
		else if (*tok)
			die("Unknown option: %s", tok);
	}
	tal_free(copy);
	return options;
}

/* "txid1,txid2" to a tal array of strings. */
static const char **parse_txids(const tal_t *ctx, const char *list)
{
	const char **txids = tal_arr(ctx, const char *, 0);
	char *copy = tal_strdup(ctx, list), *tok, *save = NULL;

	for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		while (*tok == ' ')
			tok++;
		if (*tok)
			tal_arr_expand(&txids, tal_strdup(txids, tok));
	}
	return txids;
}

/* Long-only options, above the range of single characters. */
enum {
	OPT_INPUTS = 256,
	OPT_OUTPUTS,
};

int main(int argc, char *argv[])
{
	static const struct option long_opts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "rpc", no_argument, NULL, 'p' },
		{ "testnet", no_argument, NULL, 'T' },
		{ "smartbit", no_argument, NULL, 's' },
		{ "blockstream", no_argument, NULL, 'b' },
		{ "mempool", no_argument, NULL, 'm' },
		{ "file", required_argument, NULL, 'f' },
		{ "inputs", required_argument, NULL, OPT_INPUTS },
		{ "outputs", required_argument, NULL, OPT_OUTPUTS },
		{ "txids", required_argument, NULL, 't' },
		{ "duration", required_argument, NULL, 'd' },
		{ "options", required_argument, NULL, 'o' },
		{ "cjmaxfeeratio", required_argument, NULL, 'r' },
		{ "maxnbtxos", required_argument, NULL, 'x' },
		{ NULL, 0, NULL, 0 },
	};
	tal_t *ctx = tal(NULL, char);
	const char **txids = tal_arr(ctx, const char *, 0);
	u32 options = OPT_PRECHECK | OPT_LINKABILITY | OPT_MERGE_INPUTS;
	double max_duration = 600, max_cj_intrafees_ratio = 0;
	size_t max_txos = 12;
	bool rpc = false, testnet = false, blockstream = false, mempool = false;
	const char *file = NULL, *inputs = NULL, *outputs = NULL;
	struct blockchain_provider *provider;
	struct transaction *prefetched = NULL;
	size_t i;
	int c;

	while ((c = getopt_long(argc, argv, "hpTsbmf:t:d:o:r:x:", long_opts,
				NULL)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'p':
			rpc = true;
			break;
		case 'T':
			testnet = true;
			break;
		case 's':
			die("The Smartbit API has been discontinued; use --blockstream or --mempool.");
		case 'b':
			blockstream = true;
			break;
		case 'm':
			mempool = true;
			break;
		case 'f':
			file = optarg;
			break;
		case OPT_INPUTS:
			inputs = optarg;
			break;
		case OPT_OUTPUTS:
			outputs = optarg;
			break;
		case 't':
			txids = parse_txids(ctx, optarg);
			break;
		case 'd':
			max_duration = atof(optarg);
			break;
		case 'o':
			options = parse_options(optarg);
			break;
		case 'r':
			max_cj_intrafees_ratio = atof(optarg);
			break;
		case 'x':
			max_txos = (size_t)atoi(optarg);
			break;
		default:
			usage();
			return 2;
		}
	}

	if ((inputs == NULL) != (outputs == NULL))
		die("--inputs and --outputs go together");
	if (inputs && file)
		die("--file and --inputs/--outputs are exclusive");

	if (inputs)
		provider = inline_provider(ctx, inputs, outputs);
	else if (file)
		provider = file_provider(ctx, file);
	else if (rpc)
		provider = bitcoind_rpc_provider(ctx);
	else if (blockstream)
		provider = blockstream_provider(ctx);
	else if (mempool)
		provider = mempool_space_provider(ctx);
	else
		provider = blockchain_info_provider(ctx);

	/* A file or an inline spec holds one transaction; its txid is
	 * inside.  Keep the transaction: stdin cannot be read twice.
	 */
	if ((file || inputs) && tal_count(txids) == 0) {
		char *err = NULL;

		prefetched = provider->get_tx(ctx, provider, "", !testnet, &err);
		if (!prefetched)
			die("%s", err);
		tal_arr_expand(&txids, tal_strdup(txids, prefetched->txid));
	}

	printf("DEBUG: Using %s\n", provider->description);

	for (i = 0; i < tal_count(txids); i++) {
		tal_t *tx_ctx = tal(ctx, char);
		char *err = NULL;
		struct transaction *tx;
		struct tx_analysis *an;

		printf("\n\n--- %s -------------------------------------\n", txids[i]);
		if (prefetched) {
			tx = tal_steal(tx_ctx, prefetched);
			prefetched = NULL;
		} else {
			tx = provider->get_tx(tx_ctx, provider, txids[i],
					      !testnet, &err);
		}
		if (!tx) {
			printf("Unable to retrieve information for %s from %s: %s\n",
			       txids[i], provider->description, err);
			tal_free(tx_ctx);
			continue;
		}
		printf("DEBUG: Tx fetched: %s\n", transaction_str(tx_ctx, tx));

		an = process_tx(tx_ctx, tx, options, max_duration, max_txos,
				max_cj_intrafees_ratio);
		display_results(an);
		tal_free(tx_ctx);
	}
	tal_free(ctx);
	return 0;
}
