#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 512

/* Splits `line` in place on '|'. Returns the number of fields, or -1 if there are more than max. */
static int split_fields(char *line, char **fields, int max) {
    int n = 0;
    char *p = line;
    for (;;) {
        if (n == max)
            return -1;
        fields[n++] = p;
        char *bar = strchr(p, '|');
        if (!bar)
            return n;
        *bar = '\0';
        p = bar + 1;
    }
}

static void strip_newline(char *line) {
    line[strcspn(line, "\r\n")] = '\0';
}

static int parse_ul(const char *s, unsigned long *out) {
    if (*s < '0' || *s > '9')
        return 0;
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || *end)
        return 0;
    *out = v;
    return 1;
}

static int parse_time(const char *s, time_t *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || errno || *end)
        return 0;
    *out = (time_t)v;
    return 1;
}

static int parse_amount(const char *s, Money *out) {
    if (*s == '-') {
        if (money_parse(s + 1, out) != BB_OK)
            return 0;
        *out = -*out;
        return 1;
    }
    return money_parse(s, out) == BB_OK;
}

/* accounts.db: number|username|pin_hash|balance|created|last_interest */
static void load_accounts(FILE *f, AccountList *list) {
    char line[LINE_MAX_LEN];
    Account **tail = &list->head;
    while (fgets(line, sizeof line, f)) {
        strip_newline(line);
        char *fld[6];
        if (split_fields(line, fld, 6) != 6)
            continue;
        unsigned long number, pin_hash;
        Money balance;
        time_t created, last_interest;
        if (!parse_ul(fld[0], &number) || !parse_ul(fld[2], &pin_hash) ||
            !parse_amount(fld[3], &balance) || !parse_time(fld[4], &created) ||
            !parse_time(fld[5], &last_interest) || fld[1][0] == '\0' ||
            strlen(fld[1]) > USERNAME_MAX || account_find(list, number))
            continue;
        Account *acc = calloc(1, sizeof *acc);
        if (!acc)
            break;
        acc->account_number = number;
        strcpy(acc->username, fld[1]);
        acc->pin_hash = (unsigned int)pin_hash;
        acc->balance = balance;
        acc->created_at = created;
        acc->last_interest_applied = last_interest;
        acc->next_tx_id = 1;
        *tail = acc;
        tail = &acc->next;
        if (number > list->last_account_number)
            list->last_account_number = number;
    }
}

/* Appends a loaded transaction after `*tail` (the file lists them newest first). */
static Transaction *append_tx(Account *acc, Transaction *tail) {
    Transaction *tx = calloc(1, sizeof *tx);
    if (!tx)
        return NULL;
    if (tail)
        tail->next = tx;
    else
        acc->tx_head = tx;
    return tx;
}

/* Last transaction of each account, used while loading to keep file order. */
static Transaction *last_tx(const Account *acc) {
    Transaction *t = acc->tx_head;
    while (t && t->next)
        t = t->next;
    return t;
}

/* transactions.db: account|tx_id|type|amount|balance_after|timestamp|category */
static void load_transactions(FILE *f, AccountList *list) {
    char line[LINE_MAX_LEN];
    Account *cached = NULL;
    Transaction *cached_tail = NULL;
    while (fgets(line, sizeof line, f)) {
        strip_newline(line);
        char *fld[7];
        if (split_fields(line, fld, 7) != 7)
            continue;
        unsigned long number, tx_id;
        Money amount, balance_after;
        time_t ts;
        if (!parse_ul(fld[0], &number) || !parse_ul(fld[1], &tx_id) || strlen(fld[2]) != 1 ||
            !tx_type_valid(fld[2][0]) || !parse_amount(fld[3], &amount) ||
            !parse_amount(fld[4], &balance_after) || !parse_time(fld[5], &ts) ||
            strlen(fld[6]) > CATEGORY_MAX)
            continue;
        if (!cached || cached->account_number != number) {
            cached = account_find(list, number);
            if (!cached)
                continue; /* orphaned transaction */
            cached_tail = last_tx(cached);
        }
        Transaction *tx = append_tx(cached, cached_tail);
        if (!tx)
            break;
        tx->tx_id = tx_id;
        tx->type = (TxType)fld[2][0];
        tx->amount = amount;
        tx->balance_after = balance_after;
        tx->timestamp = ts;
        strcpy(tx->category, fld[6]);
        cached_tail = tx;
        if (tx_id >= cached->next_tx_id)
            cached->next_tx_id = tx_id + 1;
    }
}

int load_data(AccountList *list) {
    account_list_init(list);
    FILE *fa = fopen(ACCOUNTS_FILE, "r");
    if (fa) {
        load_accounts(fa, list);
        fclose(fa);
    }
    FILE *ft = fopen(TRANSACTIONS_FILE, "r");
    if (ft) {
        load_transactions(ft, list);
        fclose(ft);
    }
    return 0;
}

int save_data(const AccountList *list) {
    FILE *fa = fopen(ACCOUNTS_FILE, "w");
    if (!fa)
        return -1;
    for (const Account *acc = list->head; acc; acc = acc->next) {
        char balance[MONEY_STR_MAX];
        money_format_plain(acc->balance, balance, sizeof balance);
        fprintf(fa, "%lu|%s|%u|%s|%ld|%ld\n", acc->account_number, acc->username, acc->pin_hash,
                balance, (long)acc->created_at, (long)acc->last_interest_applied);
    }
    fclose(fa);

    FILE *ft = fopen(TRANSACTIONS_FILE, "w");
    if (!ft)
        return -1;
    for (const Account *acc = list->head; acc; acc = acc->next) {
        for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
            char amount[MONEY_STR_MAX], after[MONEY_STR_MAX];
            money_format_plain(tx->amount, amount, sizeof amount);
            money_format_plain(tx->balance_after, after, sizeof after);
            fprintf(ft, "%lu|%lu|%c|%s|%s|%ld|%s\n", acc->account_number, tx->tx_id, (char)tx->type,
                    amount, after, (long)tx->timestamp, tx->category);
        }
    }
    fclose(ft);
    return 0;
}
