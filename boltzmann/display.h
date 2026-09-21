#ifndef BOLTZMANN_DISPLAY_H
#define BOLTZMANN_DISPLAY_H
/*
 * Printing the analysis of a transaction, in the exact format of the
 * Python reference (ludwig.display_results).
 */
#include "boltzmann/tx_processor.h"

/**
 * display_results - print everything we know about a transaction.
 */
void display_results(const struct tx_analysis *an);

#endif /* BOLTZMANN_DISPLAY_H */
