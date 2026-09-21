#ifndef BOLTZMANN_COMMON_PYFMT_H
#define BOLTZMANN_COMMON_PYFMT_H
/*
 * Python- and numpy-compatible text formatting.
 *
 * The reference implementation prints its results with Python's %-formats,
 * str(float) and numpy's array printer.  Its output is the contract this
 * port has to honour, so the few formatting rules involved are reproduced
 * here rather than approximated.
 */
#include "common/tal.h"
#include "common/types.h"

/**
 * py_float_str - Python's str(float): the shortest round-tripping decimal,
 * fixed notation for exponents in [-4, 16), otherwise "1e-05" style.
 */
char *py_float_str(const tal_t *ctx, double x);

/**
 * numpy_str_f64_matrix - str() of a 2-D float64 numpy array.
 *
 * Default numpy print options: precision 8, linewidth 75, no summarising
 * (matrices here are far below the 1000-element threshold).
 */
char *numpy_str_f64_matrix(const tal_t *ctx, const double *m,
			   size_t rows, size_t cols);

/**
 * numpy_str_s64_matrix - str() of a 2-D int64 numpy array.
 */
char *numpy_str_s64_matrix(const tal_t *ctx, const s64 *m,
			   size_t rows, size_t cols);

#endif /* BOLTZMANN_COMMON_PYFMT_H */
