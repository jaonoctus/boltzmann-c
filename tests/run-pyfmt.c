/*
 * Tests for the Python/numpy-compatible formatters.
 *
 * Without arguments: fixed cases whose expected strings were produced by
 * CPython and numpy 2.x.  With "--matrix": read "ROWS COLS v v v ..." from
 * stdin and print the numpy rendering, for compare_numpy_print.sh.
 */
#include "common/pyfmt.h"
#include "common/tal.h"
#include "common/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("FAIL %s:\n  got  %s\n  want %s\n", what, got, want);
		failures++;
	}
}

static void check_float(double x, const char *want)
{
	char *s = py_float_str(NULL, x);

	expect("py_float_str", s, want);
	tal_free(s);
}

static void check_matrix(const double *m, size_t rows, size_t cols,
			 const char *want)
{
	char *s = numpy_str_f64_matrix(NULL, m, rows, cols);

	expect("numpy_str_f64_matrix", s, want);
	tal_free(s);
}

static int matrix_from_stdin(void)
{
	size_t rows, cols, i;
	double *m;
	char *s;

	if (scanf("%zu %zu", &rows, &cols) != 2)
		die("expected ROWS COLS");
	m = tal_arr(NULL, double, rows * cols);
	for (i = 0; i < rows * cols; i++) {
		if (scanf("%lf", &m[i]) != 1)
			die("expected %zu values", rows * cols);
	}
	s = numpy_str_f64_matrix(NULL, m, rows, cols);
	printf("%s\n", s);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "--matrix") == 0)
		return matrix_from_stdin();

	/* str(float) */
	check_float(0.000312, "0.000312");
	check_float(1.5e-05, "1.5e-05");
	check_float(0.1 + 0.2, "0.30000000000000004");
	check_float(1.0, "1.0");
	check_float(1e16, "1e+16");
	check_float(123456789012345.0, "123456789012345.0");
	check_float(0.0, "0.0");

	/* numpy positional */
	{
		double a[] = { 1.0 / 3, 2.0 / 3, 1.0, 0.0 };
		double b[] = { 0.5, 0.25, 1.0, 0.0 };
		double c[] = { 0.123456789, 0.5 };
		double d[] = { 0.999999999, 0.5 };
		double e[] = { 1.0 / 512, 511.0 / 512 };

		check_matrix(a, 2, 2, "[[0.33333333 0.66666667]\n [1.         0.        ]]");
		check_matrix(b, 2, 2, "[[0.5  0.25]\n [1.   0.  ]]");
		check_matrix(c, 1, 2, "[[0.12345679 0.5       ]]");
		check_matrix(d, 1, 2, "[[1.  0.5]]");
		check_matrix(e, 1, 2, "[[0.00195312 0.99804688]]");
	}
	/* numpy scientific */
	{
		double a[] = { 1e-5, 0.5, 1.0, 0.0 };
		double b[] = { 9.9e-5, 0.5, 1.0, 0.0 };
		double c[] = { 1e-5, 2.0 / 3, 1.0, 0.0, 1e-9, 0.5 };
		double d[] = { 1e-3, 1.0 };

		check_matrix(a, 2, 2, "[[1.e-05 5.e-01]\n [1.e+00 0.e+00]]");
		check_matrix(b, 2, 2, "[[9.9e-05 5.0e-01]\n [1.0e+00 0.0e+00]]");
		check_matrix(c, 2, 3, "[[1.00000000e-05 6.66666667e-01 1.00000000e+00]\n"
				      " [0.00000000e+00 1.00000000e-09 5.00000000e-01]]");
		check_matrix(d, 1, 2, "[[0.001 1.   ]]");
	}
	/* line wrapping at 75 columns */
	{
		double w[14];
		size_t i;

		for (i = 0; i < 14; i++)
			w[i] = 2.0 / 3;
		check_matrix(w, 2, 7,
			     "[[0.66666667 0.66666667 0.66666667 0.66666667 0.66666667 0.66666667\n"
			     "  0.66666667]\n"
			     " [0.66666667 0.66666667 0.66666667 0.66666667 0.66666667 0.66666667\n"
			     "  0.66666667]]");
	}
	/* integers */
	{
		s64 m[] = { 0, 10, 100, 1 };
		char *s = numpy_str_s64_matrix(NULL, m, 2, 2);

		expect("numpy_str_s64_matrix", s, "[[  0  10]\n [100   1]]");
		tal_free(s);
	}

	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
