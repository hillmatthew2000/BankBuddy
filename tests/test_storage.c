#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* stat */
#endif

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "check.h"
#include "storage.h"

#define PBKDF2_SAMPLE                                      \
    "pbkdf2-sha256:1000:000102030405060708090a0b0c0d0e0f:" \
    "2240ea2a22754a3c4610356ef8e4d7acf25af2ac3d2d11866de97a8b7812c52c"

static void path_in(char *buf, size_t size, const char *name) {
    snprintf(buf, size, "%s/%s", test_temp_dir(), name);
}

static void write_file(const char *name, const char *text) {
    char path[1024];
    path_in(path, sizeof path, name);
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void remove_files(void) {
    const char *names[] = {DATA_FILE, DATA_FILE ".tmp", LEGACY_ACCOUNTS, LEGACY_TRANSACTIONS};
    char path[1024];
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        path_in(path, sizeof path, names[i]);
        remove(path);
    }
}

static void test_missing_file_is_empty(void) {
    remove_files();
    AccountList list;
    LoadReport report;
    CHECK_INT_EQ(storage_load(test_temp_dir(), &list, &report), BB_OK);
    CHECK(list.head == NULL);
    CHECK(!report.migrated_legacy);
    CHECK_INT_EQ(list.last_account_number, FIRST_ACCOUNT_NUMBER);
}

static void test_round_trip(void) {
    remove_files();
    AccountList list;
    account_list_init(&list);
    Account *a = account_create(&list, "Mary-Jane O'Neil", "fnv1a:3735928559", 1700000000);
    Account *b = account_create(&list, "bob", PBKDF2_SAMPLE, 1700000001);
    Account *c = account_create(&list, "carl", "fnv1a:1", 1700000002);
    CHECK(a && b && c);
    if (!a || !b || !c)
        return;
    account_deposit(a, 123456, "Salary", 1700000100);
    account_withdraw(a, 6, "food & drink", 1700000200);
    account_deposit(b, 1, "", 1700000300);
    account_transfer(a, b, 5000, "Rent", 1700000400);
    account_deposit(c, 500, "", 1700000500);
    account_transfer(c, a, 500, "Payback", 1700000600);
    CHECK_INT_EQ(account_close(c, 1700000700), BB_OK);
    a->failed_attempts = 2;
    a->lock_until = 1700000999;

    CHECK_INT_EQ(storage_save(test_temp_dir(), &list), BB_OK);
    AccountList loaded;
    CHECK_INT_EQ(storage_load(test_temp_dir(), &loaded, NULL), BB_OK);

    /* Same order, same fields, same transaction history (newest first). */
    const Account *x = list.head, *y = loaded.head;
    for (; x && y; x = x->next, y = y->next) {
        CHECK_INT_EQ(y->account_number, x->account_number);
        CHECK_STR_EQ(y->username, x->username);
        CHECK_STR_EQ(y->pin_credential, x->pin_credential);
        CHECK_INT_EQ(y->balance, x->balance);
        CHECK_INT_EQ(y->created_at, x->created_at);
        CHECK_INT_EQ(y->failed_attempts, x->failed_attempts);
        CHECK_INT_EQ(y->lock_until, x->lock_until);
        CHECK_INT_EQ(y->closed_at, x->closed_at);
        CHECK_INT_EQ(y->next_tx_id, x->next_tx_id);
        const Transaction *s = x->tx_head, *t = y->tx_head;
        for (; s && t; s = s->next, t = t->next) {
            CHECK_INT_EQ(t->tx_id, s->tx_id);
            CHECK_INT_EQ(t->type, s->type);
            CHECK_INT_EQ(t->amount, s->amount);
            CHECK_INT_EQ(t->balance_after, s->balance_after);
            CHECK_INT_EQ(t->timestamp, s->timestamp);
            CHECK_INT_EQ(t->counterparty, s->counterparty);
            CHECK_STR_EQ(t->category, s->category);
        }
        CHECK(s == NULL && t == NULL);
    }
    CHECK(x == NULL && y == NULL);
    CHECK_INT_EQ(loaded.last_account_number, list.last_account_number);

    /* No temporary file is left behind. */
    char tmp[1024];
    path_in(tmp, sizeof tmp, DATA_FILE ".tmp");
    FILE *f = fopen(tmp, "rb");
    CHECK(f == NULL);
    if (f)
        fclose(f);

    account_free_all(&list);
    account_free_all(&loaded);
    remove_files();
}

/* On POSIX systems the data file is readable by its owner only, even if a leftover temporary
 * file with wider access is in the way. Windows files inherit the folder's permissions. */
static void test_data_file_is_private(void) {
#ifndef _WIN32
    remove_files();
    write_file(DATA_FILE ".tmp", "left over from a crash\n");
    AccountList list;
    account_list_init(&list);
    CHECK_INT_EQ(storage_save(test_temp_dir(), &list), BB_OK);
    char path[1024];
    struct stat st;
    path_in(path, sizeof path, DATA_FILE);
    CHECK(stat(path, &st) == 0);
    CHECK_INT_EQ(st.st_mode & 0777, 0600);
    remove_files();
#endif
}

static void test_legacy_migration(void) {
    remove_files();
    /* Exactly what earlier releases wrote (CRLF line endings, newest transactions first). */
    write_file(LEGACY_ACCOUNTS, "100002|hill|2225039093|10019.00|1758143817|1758143875\r\n"
                                "100001|demo|4257489661|500.00|1758143802|1758143802\r\n"
                                "garbage line\r\n");
    write_file(LEGACY_TRANSACTIONS, "100002|3|W|6.00|10019.00|1758209794|General\r\n"
                                    "100002|2|I|25.00|10025.00|1758143875|Interest\r\n"
                                    "100002|1|D|10000.00|10000.00|1758143852|usd\r\n"
                                    "100001|1|D|500.00|500.00|1758143802|Seed\r\n"
                                    "999999|1|D|1.00|1.00|1758143802|Orphan\r\n");
    AccountList list;
    LoadReport report;
    CHECK_INT_EQ(storage_load(test_temp_dir(), &list, &report), BB_OK);
    CHECK(report.migrated_legacy);
    CHECK_INT_EQ(report.skipped_legacy, 2); /* the garbage line and the orphan */
    const Account *hill = account_find(&list, 100002);
    CHECK(hill != NULL);
    if (hill) {
        CHECK_STR_EQ(hill->username, "hill");
        CHECK_STR_EQ(hill->pin_credential, "fnv1a:2225039093");
        CHECK_INT_EQ(hill->balance, 1001900);
        CHECK_INT_EQ(hill->next_tx_id, 4);
        CHECK(hill->tx_head && hill->tx_head->tx_id == 3);
        CHECK_INT_EQ(hill->tx_head ? hill->tx_head->amount : 0, 600);
    }
    CHECK_INT_EQ(list.last_account_number, 100002);

    /* Once saved in the new format, the new file wins over the legacy ones. */
    CHECK_INT_EQ(storage_save(test_temp_dir(), &list), BB_OK);
    account_free_all(&list);
    CHECK_INT_EQ(storage_load(test_temp_dir(), &list, &report), BB_OK);
    CHECK(!report.migrated_legacy);
    CHECK(account_find(&list, 100001) != NULL);
    account_free_all(&list);
    remove_files();
}

/* Version 2 files (no closed date, no counterparty) load and are saved as the current version. */
static void test_version_2_upgrade(void) {
    remove_files();
    write_file(DATA_FILE, "BANKBUDDY 2\n"
                          "A|100001|demo|fnv1a:4257489661|500.00|1758143802|1758143802|1|0\n"
                          "T|100001|2|W|1.00|500.00|1758143803|Coffee\n"
                          "T|100001|1|D|501.00|501.00|1758143802|Seed\n");
    AccountList list;
    CHECK_INT_EQ(storage_load(test_temp_dir(), &list, NULL), BB_OK);
    const Account *acc = account_find(&list, 100001);
    CHECK(acc != NULL);
    if (acc) {
        CHECK_INT_EQ(acc->closed_at, 0);
        CHECK_INT_EQ(acc->failed_attempts, 1);
        CHECK_INT_EQ(acc->next_tx_id, 3);
        CHECK(acc->tx_head && acc->tx_head->counterparty == 0);
        CHECK_STR_EQ(acc->tx_head ? acc->tx_head->category : "", "Coffee");
    }
    CHECK_INT_EQ(storage_save(test_temp_dir(), &list), BB_OK);
    account_free_all(&list);

    char path[1024], header[32] = "";
    path_in(path, sizeof path, DATA_FILE);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(header, sizeof header, f) != NULL);
        fclose(f);
    }
    CHECK_STR_EQ(header, "BANKBUDDY 3\n");
    remove_files();
}

static void expect_load_error(const char *content, BbStatus expected, unsigned long line) {
    remove_files();
    write_file(DATA_FILE, content);
    AccountList list;
    LoadReport report;
    CHECK_INT_EQ(storage_load(test_temp_dir(), &list, &report), expected);
    CHECK(list.head == NULL);
    if (line)
        CHECK_INT_EQ(report.error_line, line);
}

static void test_damaged_files_are_refused(void) {
    const char *account = "A|100001|demo|fnv1a:4257489661|500.00|1758143802|1758143802|0|0\n";
    char text[512];

    expect_load_error("", BB_ERR_CORRUPT, 1);
    expect_load_error("NOT A BANKBUDDY FILE\n", BB_ERR_CORRUPT, 1);
    expect_load_error("BANKBUDDY 4\n", BB_ERR_VERSION, 0);

    snprintf(text, sizeof text, "BANKBUDDY 2\n%s%s", account, account);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* duplicate account */

    snprintf(text, sizeof text, "BANKBUDDY 2\nT|100001|1|D|5.00|5.00|1|Seed\n");
    expect_load_error(text, BB_ERR_CORRUPT, 2); /* transaction without an account */

    snprintf(text, sizeof text, "BANKBUDDY 2\n%sT|100001|1|D|5.001|5.00|1|Seed\n", account);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* sub-cent amount */

    snprintf(text, sizeof text, "BANKBUDDY 2\n%sT|100001|1|X|5.00|5.00|1|Seed\n", account);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* unknown type */

    snprintf(text, sizeof text,
             "BANKBUDDY 2\nA|100001|demo|fnv1a:4257489661|nan|1758143802|1758143802|0|0\n");
    expect_load_error(text, BB_ERR_CORRUPT, 2);

    snprintf(text, sizeof text,
             "BANKBUDDY 2\nA|100001|demo|pbkdf2-sha256:1:00:00|5.00|1758143802|1758143802|0|0\n");
    expect_load_error(text, BB_ERR_CORRUPT, 2); /* malformed credential */

    snprintf(text, sizeof text, "BANKBUDDY 2\n%sT|100001|1|S|5.00|5.00|1|Rent\n", account);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* version 2 had no transfers */

    const char *account3 = "A|100001|demo|fnv1a:4257489661|500.00|1758143802|1758143802|0|0|0\n";
    snprintf(text, sizeof text, "BANKBUDDY 3\n%s", account);
    expect_load_error(text, BB_ERR_CORRUPT, 2); /* version 3 accounts have a closed field */
    snprintf(text, sizeof text, "BANKBUDDY 3\n%sT|100001|1|D|5.00|5.00|1|100002|Seed\n", account3);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* a deposit has no counterparty */
    snprintf(text, sizeof text, "BANKBUDDY 3\n%sT|100001|1|R|5.00|5.00|1|0|Rent\n", account3);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* a transfer needs one */
    snprintf(text, sizeof text, "BANKBUDDY 3\n%sT|100001|1|R|5.00|5.00|1|100001|Rent\n", account3);
    expect_load_error(text, BB_ERR_CORRUPT, 3); /* ...and not itself */
    remove_files();
}

void suite_storage(void) {
    test_missing_file_is_empty();
    test_round_trip();
    test_data_file_is_private();
    test_legacy_migration();
    test_version_2_upgrade();
    test_damaged_files_are_refused();
}
