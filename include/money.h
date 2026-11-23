#ifndef MONEY_H
#define MONEY_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

/*
 * Money is held as a signed count of cents (minor units). Integer arithmetic keeps every
 * balance exact; binary floating point cannot represent most decimal amounts and drifts.
 */
typedef int64_t Money;

/* Largest amount accepted for a single transaction and for any balance: 1,000,000,000.00. */
#define MONEY_MAX ((Money)100000000000)

/* Big enough for any Money value formatted with grouping and sign. */
#define MONEY_STR_MAX 32

/*
 * Parses a non-negative decimal with at most two fractional digits, e.g. "25", "25.5",
 * "1,250.00". Commas are accepted only as thousands separators.
 * Returns BB_ERR_PARSE for malformed text and BB_ERR_RANGE above `max` (in cents).
 */
BbStatus decimal2_parse(const char *text, int64_t max, int64_t *out);

/* decimal2_parse limited to MONEY_MAX. */
BbStatus money_parse(const char *text, Money *out);

/* "1,234.56" / "-0.07": for display. Always NUL-terminates; truncates if `size` is too small. */
void money_format(Money amount, char *buf, size_t size);

/* "1234.56" / "-0.07": no grouping, for data files. */
void money_format_plain(Money amount, char *buf, size_t size);

/* Adds two amounts, failing with BB_ERR_RANGE if the result would exceed +/-MONEY_MAX. */
BbStatus money_add(Money a, Money b, Money *out);

#endif /* MONEY_H */
