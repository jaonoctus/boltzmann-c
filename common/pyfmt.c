#include "common/pyfmt.h"
#include "common/utils.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* numpy defaults. */
#define NP_PRECISION 8
#define NP_LINEWIDTH 75

/*
 * Python float repr
 */

char *py_float_str(const tal_t *ctx, double x)
{
	char buf[64], digits[32], *s;
	int prec, exp10, ndigits;

	if (isnan(x))
		return tal_strdup(ctx, "nan");
	if (isinf(x))
		return tal_strdup(ctx, x < 0 ? "-inf" : "inf");
	if (x == 0)
		return tal_strdup(ctx, signbit(x) ? "-0.0" : "0.0");

	/* Shortest %e that survives a round trip through strtod(). */
	for (prec = 0; prec < 17; prec++) {
		snprintf(buf, sizeof(buf), "%.*e", prec, x);
		if (strtod(buf, NULL) == x)
			break;
	}

	/* Pull the digits and the decimal exponent out of "d.ddde+XX". */
	{
		char *e = strchr(buf, 'e');
		const char *p;
		int n = 0;

		exp10 = atoi(e + 1);
		for (p = buf; p < e; p++) {
			if (*p >= '0' && *p <= '9')
				digits[n++] = *p;
		}
		digits[n] = '\0';
		ndigits = n;
	}

	s = tal_strdup(ctx, x < 0 ? "-" : "");
	if (exp10 >= -4 && exp10 < 16) {
		/* Fixed notation, always with a fractional part. */
		int point = exp10 + 1;		/* digits before the point */
		int i;

		if (point <= 0) {
			tal_append_fmt(&s, "0.");
			for (i = 0; i < -point; i++)
				tal_append_fmt(&s, "0");
			tal_append_fmt(&s, "%s", digits);
		} else {
			for (i = 0; i < point; i++)
				tal_append_fmt(&s, "%c", i < ndigits ? digits[i] : '0');
			tal_append_fmt(&s, ".");
			if (point >= ndigits)
				tal_append_fmt(&s, "0");
			else
				tal_append_fmt(&s, "%s", digits + point);
		}
	} else {
		tal_append_fmt(&s, "%c", digits[0]);
		if (ndigits > 1)
			tal_append_fmt(&s, ".%s", digits + 1);
		tal_append_fmt(&s, "e%c%02d", exp10 < 0 ? '-' : '+', abs(exp10));
	}
	return s;
}

/*
 * numpy array printing
 *
 * Two steps, as in numpy: first every element is turned into a string and
 * all strings are padded to a common width, then the strings are laid out
 * with brackets, wrapping rows that would exceed the line width.
 */

/**
 * layout_matrix - numpy's _formatArray for a 2-D array of equal-width cells.
 *
 * A row is "[" + cells joined by " " + "]".  Rows are joined with "\n "
 * and the whole thing wrapped in another pair of brackets, so the first
 * row starts with "[[" and every other row with " [".  Within a row, a
 * cell that would push the line past the width limit starts a new line
 * indented by two spaces.
 */
static char *layout_matrix(const tal_t *ctx, char **cells, size_t rows,
			   size_t cols)
{
	char *out = tal_strdup(ctx, "[");
	size_t r, c;

	/* Inside a row the hanging indent is two characters ("[[" or " [")
	 * and the closing bracket reserves one column.
	 */
	const size_t indent = 2;
	const size_t width_limit = NP_LINEWIDTH - 1 - 1;

	for (r = 0; r < rows; r++) {
		size_t line_len = indent;

		if (r > 0)
			tal_append_fmt(&out, "\n ");
		tal_append_fmt(&out, "[");
		for (c = 0; c < cols; c++) {
			const char *cell = cells[r * cols + c];
			size_t len = strlen(cell);

			if (c > 0) {
				/* Wrap unless the line is still empty.  numpy
				 * rstrip()s the line it wraps, which matters
				 * when the last cell was padded on the right.
				 */
				if (line_len + len > width_limit && line_len > indent) {
					size_t end = strlen(out);

					while (end > 0 && out[end - 1] == ' ')
						end--;
					out[end] = '\0';
					tal_append_fmt(&out, "\n  ");
					line_len = indent;
				} else {
					tal_append_fmt(&out, " ");
					line_len++;
				}
			}
			tal_append_fmt(&out, "%s", cell);
			line_len += len;
		}
		tal_append_fmt(&out, "]");
	}
	tal_append_fmt(&out, "]");
	return out;
}

/* Remove trailing zeros after the decimal point, keeping the point:
 * "1.00000000" -> "1.", "0.50000000" -> "0.5".
 */
static void strip_fraction_zeros(char *s)
{
	char *end = s + strlen(s);

	while (end > s && end[-1] == '0')
		end--;
	*end = '\0';
}

/**
 * format_positional - "0.66666667", "1.", "0.5": up to 8 decimals with
 * trailing zeros removed, then padded so the decimal points line up.
 */
static char **format_positional(const tal_t *ctx, const double *m, size_t n)
{
	char **cells = tal_arr(ctx, char *, n);
	char **ints = tal_arr(ctx, char *, n);
	char **fracs = tal_arr(ctx, char *, n);
	size_t pad_left = 0, pad_right = 0, i;

	for (i = 0; i < n; i++) {
		char buf[64], *dot;

		snprintf(buf, sizeof(buf), "%.*f", NP_PRECISION, m[i]);
		dot = strchr(buf, '.');
		*dot = '\0';
		strip_fraction_zeros(dot + 1);
		ints[i] = tal_strdup(ints, buf);
		fracs[i] = tal_strdup(fracs, dot + 1);
		if (strlen(ints[i]) > pad_left)
			pad_left = strlen(ints[i]);
		if (strlen(fracs[i]) > pad_right)
			pad_right = strlen(fracs[i]);
	}
	for (i = 0; i < n; i++)
		cells[i] = tal_fmt(cells, "%*s.%-*s", (int)pad_left, ints[i],
				   (int)pad_right, fracs[i]);
	tal_free(ints);
	tal_free(fracs);
	return cells;
}

/**
 * format_scientific - "6.66666667e-01", "1.e+00": mantissa with trailing
 * zeros removed, then zero-padded to a common number of digits.
 */
static char **format_scientific(const tal_t *ctx, const double *m, size_t n)
{
	char **cells = tal_arr(ctx, char *, n);
	char **mants = tal_arr(ctx, char *, n);
	char **exps = tal_arr(ctx, char *, n);
	size_t pad_right = 0, i;

	for (i = 0; i < n; i++) {
		char buf[64], *e;

		snprintf(buf, sizeof(buf), "%.*e", NP_PRECISION, m[i]);
		e = strchr(buf, 'e');
		*e = '\0';
		strip_fraction_zeros(buf);
		mants[i] = tal_strdup(mants, buf);
		exps[i] = tal_strdup(exps, e + 1);
		if (strlen(strchr(mants[i], '.') + 1) > pad_right)
			pad_right = strlen(strchr(mants[i], '.') + 1);
	}
	for (i = 0; i < n; i++) {
		size_t frac_len = strlen(strchr(mants[i], '.') + 1), k;

		cells[i] = tal_strdup(cells, mants[i]);
		for (k = frac_len; k < pad_right; k++)
			tal_append_fmt(&cells[i], "0");
		tal_append_fmt(&cells[i], "e%s", exps[i]);
	}
	tal_free(mants);
	tal_free(exps);
	return cells;
}

char *numpy_str_f64_matrix(const tal_t *ctx, const double *m,
			   size_t rows, size_t cols)
{
	size_t n = rows * cols, i;
	double max_abs = 0, min_abs = 0;
	bool any_nonzero = false, scientific;
	char **cells, *s;

	/* numpy switches to scientific notation when the values span more
	 * than three orders of magnitude, or get very small or very large.
	 */
	for (i = 0; i < n; i++) {
		double a = fabs(m[i]);

		if (a == 0)
			continue;
		if (!any_nonzero || a > max_abs)
			max_abs = a;
		if (!any_nonzero || a < min_abs)
			min_abs = a;
		any_nonzero = true;
	}
	scientific = any_nonzero &&
		     (max_abs >= 1e8 || min_abs < 1e-4 || max_abs / min_abs > 1e3);

	cells = scientific ? format_scientific(ctx, m, n)
			   : format_positional(ctx, m, n);
	s = layout_matrix(ctx, cells, rows, cols);
	tal_free(cells);
	return s;
}

char *numpy_str_s64_matrix(const tal_t *ctx, const s64 *m,
			   size_t rows, size_t cols)
{
	size_t n = rows * cols, i, width = 0;
	char **cells = tal_arr(ctx, char *, n);
	char *s;

	for (i = 0; i < n; i++) {
		cells[i] = tal_fmt(cells, "%lld", (long long)m[i]);
		if (strlen(cells[i]) > width)
			width = strlen(cells[i]);
	}
	for (i = 0; i < n; i++)
		cells[i] = tal_fmt(cells, "%*s", (int)width, cells[i]);
	s = layout_matrix(ctx, cells, rows, cols);
	tal_free(cells);
	return s;
}
