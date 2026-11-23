#ifndef INTEREST_H
#define INTEREST_H

#include "account.h"

/* Rates are basis points: 1 bp = 0.01%, so 475 means 4.75% per year. */
#define INTEREST_MAX_RATE_BP 10000 /* 100% a year */
#define INTEREST_MAX_PERIODS 365   /* daily compounding */

/* Parses a yearly percentage such as "5" or "4.75" into basis points (0..100%). */
BbStatus interest_parse_rate(const char *text, int *rate_bp);

/*
 * Interest for one period: balance * rate / periods_per_year, rounded half up to the cent.
 * Fails with BB_ERR_RANGE for a rate or period count outside the limits above.
 */
BbStatus interest_for_period(Money balance, int rate_bp, unsigned periods_per_year,
                             Money *interest);

/*
 * Credits one period of interest to the account and records it. `*credited` receives the
 * amount, which may be zero (nothing is recorded then, e.g. for a tiny balance).
 */
BbStatus apply_interest(Account *acc, int rate_bp, unsigned periods_per_year, time_t now,
                        Money *credited);

#endif /* INTEREST_H */
