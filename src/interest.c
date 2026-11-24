#include "interest.h"

#define INTEREST_CATEGORY "Interest"

BbStatus interest_parse_rate(const char *text, int *rate_bp) {
    int64_t bp;
    /* A percentage with two decimals is exactly a count of basis points. */
    BbStatus rc = decimal2_parse(text, INTEREST_MAX_RATE_BP, &bp);
    if (rc != BB_OK)
        return rc;
    *rate_bp = (int)bp;
    return BB_OK;
}

BbStatus interest_for_period(Money balance, int rate_bp, unsigned periods_per_year,
                             Money *interest) {
    if (!interest)
        return BB_ERR_INVALID_ARG;
    if (rate_bp < 0 || rate_bp > INTEREST_MAX_RATE_BP || periods_per_year == 0 ||
        periods_per_year > INTEREST_MAX_PERIODS || balance < 0 || balance > MONEY_MAX)
        return BB_ERR_RANGE;
    /* balance <= 1e11 and rate_bp <= 1e4, so the product fits comfortably in 64 bits. */
    int64_t numerator = balance * rate_bp;
    int64_t denominator = (int64_t)10000 * periods_per_year;
    *interest = (numerator + denominator / 2) / denominator;
    return BB_OK;
}

BbStatus apply_interest(Account *acc, int rate_bp, unsigned periods_per_year, time_t now,
                        Money *credited) {
    if (!acc || !credited)
        return BB_ERR_INVALID_ARG;
    *credited = 0;
    if (acc->closed_at)
        return BB_ERR_CLOSED;
    Money interest;
    BbStatus rc = interest_for_period(acc->balance, rate_bp, periods_per_year, &interest);
    if (rc != BB_OK)
        return rc;
    if (interest == 0)
        return BB_OK;
    Money new_balance;
    rc = money_add(acc->balance, interest, &new_balance);
    if (rc != BB_OK)
        return rc;
    rc =
        account_record_transaction(acc, TX_INTEREST, interest, INTEREST_CATEGORY, new_balance, now);
    if (rc != BB_OK)
        return rc;
    acc->balance = new_balance;
    acc->last_interest_applied = now;
    *credited = interest;
    return BB_OK;
}
