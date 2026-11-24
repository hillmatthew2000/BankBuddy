#ifndef STORAGE_H
#define STORAGE_H

#include "account.h"

/*
 * All data lives in one text file, DATA_FILE, inside the data directory:
 *
 *   BANKBUDDY 3
 *   A|number|username|credential|balance|created|last_interest|failed_attempts|lock_until|closed
 *   T|number|tx_id|type|amount|balance_after|timestamp|counterparty|category
 *
 * The credential is a salted PIN hash in one of the formats described in auth.h. Amounts are
 * decimal with two places ("1234.50"); times are Unix seconds and `closed` is 0 for an open
 * account. The transaction type is a TxType letter; transfers (S, R) name the other account
 * as counterparty, every other type has 0. Each account line is followed by its transactions,
 * newest first. Saving writes a temporary file and renames it over the old one, so a crash
 * mid-save never leaves a half-written file behind.
 *
 * Older formats are read and converted the next time the data is saved: version 2 is the
 * same without `closed` and `counterparty`; version 1 was two files (accounts.db and
 * transactions.db), which are left untouched.
 */
#define DATA_FILE           "bankbuddy.db"
#define DATA_FORMAT_VERSION 3
#define LEGACY_ACCOUNTS     "accounts.db"
#define LEGACY_TRANSACTIONS "transactions.db"

/* What load found, for the caller to report. */
typedef struct LoadReport {
    int migrated_legacy;          /* data came from the version 1 files */
    unsigned long skipped_legacy; /* unreadable version 1 lines that were dropped */
    unsigned long error_line;     /* on BB_ERR_CORRUPT: the offending line, 1-based */
    char path[1024];              /* the file that was read or failed */
} LoadReport;

/*
 * Loads everything from `dir` into a freshly initialised list. A missing file is not an
 * error (the list is just empty). A damaged file fails with BB_ERR_CORRUPT and a newer format
 * with BB_ERR_VERSION; nothing is loaded then, so the caller must not save over it.
 */
BbStatus storage_load(const char *dir, AccountList *list, LoadReport *report);

/* Writes everything to `dir` atomically. */
BbStatus storage_save(const char *dir, const AccountList *list);

#endif /* STORAGE_H */
