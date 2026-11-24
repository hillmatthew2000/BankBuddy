#include "storage.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "platform.h"

#define LINE_MAX_LEN 1024
#define HEADER_MAGIC "BANKBUDDY"
#define TEMP_SUFFIX  ".tmp"
#define PATH_MAX_LEN 1024

/* ---------- small helpers ---------- */

static BbStatus join_path(char *out, size_t size, const char *dir, const char *name) {
    int n = snprintf(out, size, "%s/%s", dir && *dir ? dir : ".", name);
    return (n < 0 || (size_t)n >= size) ? BB_ERR_RANGE : BB_OK;
}

/* Splits `line` in place on '|'. Returns the field count, or -1 if there are more than max. */
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

/* Reads a line without its line ending. Returns 0 at EOF, -1 if the line is too long. */
static int next_line(FILE *f, char *buf, size_t size) {
    if (!fgets(buf, (int)size, f))
        return 0;
    size_t len = strcspn(buf, "\r\n");
    if (buf[len] == '\0' && !feof(f))
        return -1;
    buf[len] = '\0';
    return 1;
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
    const char *digits = *s == '-' ? s + 1 : s;
    if (*digits < '0' || *digits > '9')
        return 0;
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || *end)
        return 0;
    *out = (time_t)v;
    return (long long)*out == v; /* reject values a 32-bit time_t cannot hold */
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

static int text_field_ok(const char *s, size_t max) {
    size_t len = strlen(s);
    if (len == 0 || len > max)
        return 0;
    for (; *s; s++)
        if ((unsigned char)*s < 0x20)
            return 0;
    return 1;
}

/* Formats a time without printf: old MinGW runtimes mishandle %lld in some functions. */
static const char *time_str(time_t t, char buf[24]) {
    long long v = (long long)t;
    unsigned long long u = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
    char *p = buf + 23;
    *p = '\0';
    do {
        *--p = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    if (v < 0)
        *--p = '-';
    return p;
}

static Account *new_account(AccountList *list, Account ***tail, unsigned long number) {
    Account *acc = calloc(1, sizeof *acc);
    if (!acc)
        return NULL;
    acc->account_number = number;
    acc->next_tx_id = 1;
    **tail = acc;
    *tail = &acc->next;
    if (number > list->last_account_number)
        list->last_account_number = number;
    return acc;
}

/* Appends a loaded transaction (files list them newest first, matching memory order). */
static Transaction *append_tx(Account *acc, Transaction **tail) {
    Transaction *tx = calloc(1, sizeof *tx);
    if (!tx)
        return NULL;
    if (*tail)
        (*tail)->next = tx;
    else
        acc->tx_head = tx;
    *tail = tx;
    return tx;
}

static int is_transfer(TxType type) {
    return type == TX_TRANSFER_OUT || type == TX_TRANSFER_IN;
}

/*
 * Parses the transaction fields every format shares: id, type, amount, balance_after and
 * timestamp in f[0..4], and the category.
 */
static int fill_tx(Transaction *tx, char **f, const char *category) {
    unsigned long id;
    if (!parse_ul(f[0], &id) || id == 0 || strlen(f[1]) != 1 || !tx_type_valid(f[1][0]) ||
        !parse_amount(f[2], &tx->amount) || tx->amount <= 0 ||
        !parse_amount(f[3], &tx->balance_after) || !parse_time(f[4], &tx->timestamp) ||
        !text_field_ok(category, CATEGORY_MAX))
        return 0;
    tx->tx_id = id;
    tx->type = (TxType)f[1][0];
    strcpy(tx->category, category);
    return 1;
}

/* ---------- versions 2 and 3 ---------- */

static int parse_credential(const char *s, Account *acc) {
    if (!pin_credential_valid(s))
        return 0;
    strcpy(acc->pin_credential, s);
    return 1;
}

/* Version 3 added the closed date to accounts and the counterparty to transactions. */
static BbStatus load_records(FILE *f, AccountList *list, LoadReport *report,
                             unsigned long version) {
    const int account_fields = version >= 3 ? 10 : 9;
    const int tx_fields = version >= 3 ? 9 : 8;
    char line[LINE_MAX_LEN];
    Account **acc_tail = &list->head;
    Account *current = NULL;
    Transaction *tx_tail = NULL;
    unsigned long line_no = 1; /* the header was line 1 */
    int rc;
    while ((rc = next_line(f, line, sizeof line)) != 0) {
        line_no++;
        report->error_line = line_no;
        if (rc < 0)
            return BB_ERR_CORRUPT;
        if (line[0] == '\0')
            continue;
        char *fld[10];
        int n = split_fields(line, fld, 10);
        if (n == account_fields && strcmp(fld[0], "A") == 0) {
            unsigned long number, failed;
            if (!parse_ul(fld[1], &number) || account_find(list, number) ||
                !text_field_ok(fld[2], USERNAME_MAX))
                return BB_ERR_CORRUPT;
            current = new_account(list, &acc_tail, number);
            if (!current)
                return BB_ERR_NO_MEMORY;
            tx_tail = NULL;
            strcpy(current->username, fld[2]);
            if (!parse_credential(fld[3], current) || !parse_amount(fld[4], &current->balance) ||
                current->balance < 0 || current->balance > MONEY_MAX ||
                !parse_time(fld[5], &current->created_at) ||
                !parse_time(fld[6], &current->last_interest_applied) ||
                !parse_ul(fld[7], &failed) || failed > 1000 ||
                !parse_time(fld[8], &current->lock_until) ||
                (version >= 3 &&
                 (!parse_time(fld[9], &current->closed_at) || current->closed_at < 0)))
                return BB_ERR_CORRUPT;
            current->failed_attempts = (unsigned)failed;
        } else if (n == tx_fields && strcmp(fld[0], "T") == 0) {
            unsigned long number, counterparty = 0;
            if (!current || !parse_ul(fld[1], &number) || number != current->account_number)
                return BB_ERR_CORRUPT;
            Transaction *tx = append_tx(current, &tx_tail);
            if (!tx)
                return BB_ERR_NO_MEMORY;
            if (!fill_tx(tx, fld + 2, fld[n - 1]) ||
                (version >= 3 && !parse_ul(fld[7], &counterparty)) ||
                is_transfer(tx->type) != (counterparty != 0) || counterparty == number)
                return BB_ERR_CORRUPT;
            tx->counterparty = counterparty;
            if (tx->tx_id >= current->next_tx_id)
                current->next_tx_id = tx->tx_id + 1;
        } else {
            return BB_ERR_CORRUPT;
        }
    }
    if (ferror(f))
        return BB_ERR_IO;
    report->error_line = 0;
    return BB_OK;
}

/* ---------- version 1 (accounts.db + transactions.db) ---------- */

/* accounts.db: number|username|pin_hash|balance|created|last_interest */
static BbStatus load_v1_accounts(FILE *f, AccountList *list, LoadReport *report) {
    char line[LINE_MAX_LEN];
    Account **tail = &list->head;
    int rc;
    while ((rc = next_line(f, line, sizeof line)) != 0) {
        char *fld[6];
        unsigned long number, hash;
        Money balance;
        time_t created, last_interest;
        if (rc < 0 || split_fields(line, fld, 6) != 6 || !parse_ul(fld[0], &number) ||
            !text_field_ok(fld[1], USERNAME_MAX) || !parse_ul(fld[2], &hash) ||
            hash > 0xFFFFFFFFUL || !parse_amount(fld[3], &balance) || balance < 0 ||
            !parse_time(fld[4], &created) || !parse_time(fld[5], &last_interest) ||
            account_find(list, number)) {
            if (line[0] != '\0')
                report->skipped_legacy++;
            continue;
        }
        Account *acc = new_account(list, &tail, number);
        if (!acc)
            return BB_ERR_NO_MEMORY;
        strcpy(acc->username, fld[1]);
        /* Version 1 stored a bare FNV-1a hash; it is re-hashed at the next login. */
        snprintf(acc->pin_credential, sizeof acc->pin_credential, "fnv1a:%lu", hash);
        acc->balance = balance;
        acc->created_at = created;
        acc->last_interest_applied = last_interest;
    }
    return ferror(f) ? BB_ERR_IO : BB_OK;
}

/* transactions.db: account|tx_id|type|amount|balance_after|timestamp|category */
static BbStatus load_v1_transactions(FILE *f, AccountList *list, LoadReport *report) {
    char line[LINE_MAX_LEN];
    Account *current = NULL;
    Transaction *tail = NULL;
    int rc;
    while ((rc = next_line(f, line, sizeof line)) != 0) {
        char *fld[7];
        unsigned long number;
        Transaction parsed;
        memset(&parsed, 0, sizeof parsed);
        if (rc < 0 || split_fields(line, fld, 7) != 7 || !parse_ul(fld[0], &number) ||
            !fill_tx(&parsed, fld + 1, fld[6]) || is_transfer(parsed.type)) {
            if (line[0] != '\0')
                report->skipped_legacy++;
            continue;
        }
        if (!current || current->account_number != number) {
            current = account_find(list, number);
            if (!current) {
                report->skipped_legacy++; /* orphaned transaction */
                continue;
            }
            tail = current->tx_head;
            while (tail && tail->next)
                tail = tail->next;
        }
        Transaction *tx = append_tx(current, &tail);
        if (!tx)
            return BB_ERR_NO_MEMORY;
        *tx = parsed;
        if (tx->tx_id >= current->next_tx_id)
            current->next_tx_id = tx->tx_id + 1;
    }
    return ferror(f) ? BB_ERR_IO : BB_OK;
}

static BbStatus load_v1(const char *dir, AccountList *list, LoadReport *report) {
    char path[PATH_MAX_LEN];
    BbStatus rc = join_path(path, sizeof path, dir, LEGACY_ACCOUNTS);
    if (rc != BB_OK)
        return rc;
    FILE *fa = fopen(path, "rb");
    if (!fa)
        return BB_OK; /* no legacy data either: first run */
    report->migrated_legacy = 1;
    strcpy(report->path, path);
    rc = load_v1_accounts(fa, list, report);
    fclose(fa);
    if (rc != BB_OK)
        return rc;

    rc = join_path(path, sizeof path, dir, LEGACY_TRANSACTIONS);
    if (rc != BB_OK)
        return rc;
    FILE *ft = fopen(path, "rb");
    if (ft) {
        rc = load_v1_transactions(ft, list, report);
        fclose(ft);
    }
    return rc;
}

/* ---------- public API ---------- */

BbStatus storage_load(const char *dir, AccountList *list, LoadReport *report) {
    LoadReport scratch;
    if (!report)
        report = &scratch;
    memset(report, 0, sizeof *report);
    if (!list)
        return BB_ERR_INVALID_ARG;
    account_list_init(list);

    BbStatus rc = join_path(report->path, sizeof report->path, dir, DATA_FILE);
    if (rc != BB_OK)
        return rc;
    FILE *f = fopen(report->path, "rb");
    if (!f) {
        rc = load_v1(dir, list, report);
    } else {
        char header[64];
        report->error_line = 1;
        if (next_line(f, header, sizeof header) != 1 ||
            strncmp(header, HEADER_MAGIC " ", strlen(HEADER_MAGIC) + 1) != 0) {
            rc = BB_ERR_CORRUPT;
        } else {
            unsigned long version;
            if (!parse_ul(header + strlen(HEADER_MAGIC) + 1, &version) || version < 2)
                rc = BB_ERR_CORRUPT;
            else if (version > DATA_FORMAT_VERSION)
                rc = BB_ERR_VERSION;
            else
                rc = load_records(f, list, report, version);
        }
        fclose(f);
    }
    if (rc != BB_OK)
        account_free_all(list);
    return rc;
}

static int write_all(FILE *f, const AccountList *list) {
    char t1[24], t2[24], t3[24], t4[24];
    if (fprintf(f, "%s %d\n", HEADER_MAGIC, DATA_FORMAT_VERSION) < 0)
        return 0;
    for (const Account *acc = list->head; acc; acc = acc->next) {
        char balance[MONEY_STR_MAX];
        money_format_plain(acc->balance, balance, sizeof balance);
        if (fprintf(f, "A|%lu|%s|%s|%s|%s|%s|%u|%s|%s\n", acc->account_number, acc->username,
                    acc->pin_credential, balance, time_str(acc->created_at, t1),
                    time_str(acc->last_interest_applied, t2), acc->failed_attempts,
                    time_str(acc->lock_until, t3), time_str(acc->closed_at, t4)) < 0)
            return 0;
        for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
            char amount[MONEY_STR_MAX], after[MONEY_STR_MAX];
            money_format_plain(tx->amount, amount, sizeof amount);
            money_format_plain(tx->balance_after, after, sizeof after);
            if (fprintf(f, "T|%lu|%lu|%c|%s|%s|%s|%lu|%s\n", acc->account_number, tx->tx_id,
                        (char)tx->type, amount, after, time_str(tx->timestamp, t1),
                        tx->counterparty, tx->category) < 0)
                return 0;
        }
    }
    return 1;
}

BbStatus storage_save(const char *dir, const AccountList *list) {
    if (!list)
        return BB_ERR_INVALID_ARG;
    char path[PATH_MAX_LEN], temp[PATH_MAX_LEN];
    if (join_path(path, sizeof path, dir, DATA_FILE) != BB_OK ||
        join_path(temp, sizeof temp, dir, DATA_FILE TEMP_SUFFIX) != BB_OK)
        return BB_ERR_RANGE;

    FILE *f = platform_open_for_commit(temp);
    if (!f)
        return BB_ERR_IO;
    int ok = write_all(f, list) && platform_sync(f) == BB_OK;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || platform_replace_file(temp, path) != BB_OK) {
        remove(temp);
        return BB_ERR_IO;
    }
    return BB_OK;
}
