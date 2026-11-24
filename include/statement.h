#ifndef STATEMENT_H
#define STATEMENT_H

#include <stdio.h>
#include <time.h>

#include "account.h"

/* Account statements: picking transactions by date and exporting them. */

/* "Deposit", "Transfer out", ...: the name shown to people for a transaction type. */
const char *tx_type_name(TxType type);

/* 1 if the transaction happened within [start, end]; a zero bound is open. */
int statement_includes(const Transaction *tx, time_t start, time_t end);

/*
 * Turns "YYYY-MM-DD" dates (local time) into a range covering both days completely. An empty
 * string leaves that end open (0). Fails with BB_ERR_PARSE for a malformed or impossible date,
 * or one this system's time_t cannot represent, and BB_ERR_RANGE if the range ends before it
 * starts.
 */
BbStatus statement_parse_range(const char *from, const char *to, time_t *start, time_t *end);

/*
 * Writes the transactions within [start, end] as CSV (RFC 4180, CRLF line endings), oldest
 * first, with the columns id, date, type, category, counterparty, amount and balance. Amounts
 * are signed: negative for money leaving the account. *rows (optional) receives the number of
 * transactions written.
 */
BbStatus statement_write_csv(const Account *acc, time_t start, time_t end, FILE *out,
                             unsigned long *rows);

#endif /* STATEMENT_H */
