#include "statement.h"

#include <stdlib.h>
#include <string.h>

const char *tx_type_name(TxType type) {
    switch (type) {
        case TX_DEPOSIT:
            return "Deposit";
        case TX_WITHDRAWAL:
            return "Withdrawal";
        case TX_INTEREST:
            return "Interest";
        case TX_TRANSFER_OUT:
            return "Transfer out";
        case TX_TRANSFER_IN:
            return "Transfer in";
    }
    return "?";
}

int statement_includes(const Transaction *tx, time_t start, time_t end) {
    return tx && (!start || tx->timestamp >= start) && (!end || tx->timestamp <= end);
}

/* ---------- dates ---------- */

static int leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* Parses exactly "YYYY-MM-DD" naming a real calendar day from 1970 on. */
static BbStatus parse_date(const char *s, int *year, int *month, int *day) {
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int v[3] = {0, 0, 0};
    const int width[3] = {4, 2, 2};
    const char *p = s;
    for (int part = 0; part < 3; part++) {
        for (int i = 0; i < width[part]; i++, p++) {
            if (*p < '0' || *p > '9')
                return BB_ERR_PARSE;
            v[part] = v[part] * 10 + (*p - '0');
        }
        if (*p != (part < 2 ? '-' : '\0'))
            return BB_ERR_PARSE;
        p++;
    }
    if (v[0] < 1970 || v[1] < 1 || v[1] > 12)
        return BB_ERR_PARSE;
    int max_day = days_in_month[v[1] - 1] + (v[1] == 2 && leap_year(v[0]));
    if (v[2] < 1 || v[2] > max_day)
        return BB_ERR_PARSE;
    *year = v[0];
    *month = v[1];
    *day = v[2];
    return BB_OK;
}

/* Local midnight at the start of the given day plus `days_later` days. */
static BbStatus local_midnight(int year, int month, int day, int days_later, time_t *out) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day + days_later; /* mktime normalises the 32nd of a month and so on */
    t.tm_isdst = -1;              /* let the C library work out daylight saving time */
    time_t when = mktime(&t);
    if (when == (time_t)-1)
        return BB_ERR_PARSE; /* beyond what this system's time_t can hold */
    *out = when;
    return BB_OK;
}

BbStatus statement_parse_range(const char *from, const char *to, time_t *start, time_t *end) {
    if (!from || !to || !start || !end)
        return BB_ERR_INVALID_ARG;
    int y, m, d;
    time_t s = 0, e = 0;
    BbStatus rc;
    if (*from) {
        if ((rc = parse_date(from, &y, &m, &d)) != BB_OK ||
            (rc = local_midnight(y, m, d, 0, &s)) != BB_OK)
            return rc;
    }
    if (*to) {
        if ((rc = parse_date(to, &y, &m, &d)) != BB_OK ||
            (rc = local_midnight(y, m, d, 1, &e)) != BB_OK)
            return rc;
        e -= 1; /* the last second of the day */
    }
    if (s && e && e < s)
        return BB_ERR_RANGE;
    *start = s;
    *end = e;
    return BB_OK;
}

/* ---------- CSV ---------- */

/*
 * Writes one text field. Fields with separators or quotes are quoted, and a leading = + - or @
 * is defused with an apostrophe so that spreadsheets do not run the text as a formula.
 */
static int write_text_field(FILE *out, const char *s) {
    int quote = strpbrk(s, ",\"\r\n") != NULL;
    if (quote && fputc('"', out) == EOF)
        return 0;
    if (s[0] != '\0' && strchr("=+-@", s[0]) && fputc('\'', out) == EOF)
        return 0;
    for (; *s; s++) {
        if (*s == '"' && fputc('"', out) == EOF)
            return 0;
        if (fputc(*s, out) == EOF)
            return 0;
    }
    return !quote || fputc('"', out) != EOF;
}

static int write_row(FILE *out, const Transaction *tx) {
    char when[32] = "";
    const struct tm *tmv = localtime(&tx->timestamp);
    if (tmv)
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tmv);
    char amount[MONEY_STR_MAX + 1], balance[MONEY_STR_MAX];
    amount[0] = '-';
    money_format_plain(tx->amount, amount + 1, sizeof amount - 1);
    money_format_plain(tx->balance_after, balance, sizeof balance);
    char counterparty[24] = "";
    if (tx->counterparty)
        snprintf(counterparty, sizeof counterparty, "%lu", tx->counterparty);

    if (fprintf(out, "%lu,%s,%s,", tx->tx_id, when, tx_type_name(tx->type)) < 0 ||
        !write_text_field(out, tx->category))
        return 0;
    return fprintf(out, ",%s,%s,%s\r\n", counterparty, tx_is_credit(tx->type) ? amount + 1 : amount,
                   balance) >= 0;
}

BbStatus statement_write_csv(const Account *acc, time_t start, time_t end, FILE *out,
                             unsigned long *rows) {
    if (!acc || !out)
        return BB_ERR_INVALID_ARG;
    if (rows)
        *rows = 0;

    /* The list is newest first; collect the matches so they can be written oldest first. */
    size_t count = 0;
    for (const Transaction *tx = acc->tx_head; tx; tx = tx->next)
        count += (size_t)statement_includes(tx, start, end);
    const Transaction **picked = NULL;
    if (count) {
        picked = malloc(count * sizeof *picked);
        if (!picked)
            return BB_ERR_NO_MEMORY;
    }
    size_t n = 0;
    for (const Transaction *tx = acc->tx_head; tx; tx = tx->next)
        if (statement_includes(tx, start, end))
            picked[n++] = tx;

    int ok = fputs("id,date,type,category,counterparty,amount,balance\r\n", out) != EOF;
    while (ok && n > 0)
        ok = write_row(out, picked[--n]);
    free(picked);
    if (!ok || ferror(out))
        return BB_ERR_IO;
    if (rows)
        *rows = (unsigned long)count;
    return BB_OK;
}
