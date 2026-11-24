#include <stdio.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "statement.h"

/* Local midnight of a date, the way the statement code computes it. */
static time_t midnight(int year, int month, int day) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void test_parse_range(void) {
    time_t start = 1, end = 1;
    CHECK_INT_EQ(statement_parse_range("", "", &start, &end), BB_OK);
    CHECK_INT_EQ(start, 0);
    CHECK_INT_EQ(end, 0);

    CHECK_INT_EQ(statement_parse_range("2024-02-28", "2024-02-29", &start, &end), BB_OK);
    CHECK_INT_EQ(start, midnight(2024, 2, 28));
    CHECK_INT_EQ(end, midnight(2024, 3, 1) - 1);

    CHECK_INT_EQ(statement_parse_range("2025-12-31", "", &start, &end), BB_OK);
    CHECK_INT_EQ(start, midnight(2025, 12, 31));
    CHECK_INT_EQ(end, 0);
    CHECK_INT_EQ(statement_parse_range("", "2025-12-31", &start, &end), BB_OK);
    CHECK_INT_EQ(start, 0);
    CHECK_INT_EQ(end, midnight(2026, 1, 1) - 1);

    /* The same day on both ends covers that whole day. */
    CHECK_INT_EQ(statement_parse_range("2025-06-15", "2025-06-15", &start, &end), BB_OK);
    CHECK(end > start);

    const char *bad[] = {"2025-6-15",  "2025/06/15", "2025-06-15 ", "2025-13-01",
                         "2025-00-10", "2025-02-29", "2100-02-29",  "2025-04-31",
                         "1969-12-31", "abcd-ef-gh", "2025-06-150"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_INT_EQ(statement_parse_range(bad[i], "", &start, &end), BB_ERR_PARSE);
    CHECK_INT_EQ(statement_parse_range("2000-02-29", "", &start, &end), BB_OK);
    CHECK_INT_EQ(statement_parse_range("2025-06-16", "2025-06-15", &start, &end), BB_ERR_RANGE);
}

static void test_includes(void) {
    Transaction tx;
    memset(&tx, 0, sizeof tx);
    tx.timestamp = 1000;
    CHECK(statement_includes(&tx, 0, 0));
    CHECK(statement_includes(&tx, 1000, 1000));
    CHECK(statement_includes(&tx, 0, 1000));
    CHECK(!statement_includes(&tx, 1001, 0));
    CHECK(!statement_includes(&tx, 0, 999));
    CHECK(!statement_includes(NULL, 0, 0));
}

/* Writes the CSV for [start, end] to a scratch file and reads it back. */
static BbStatus export(const Account *acc, time_t start, time_t end, char *buf, size_t size,
                       unsigned long *rows) {
    char path[512];
    test_temp_path(path, sizeof path, "export.csv");
    buf[0] = '\0';
    FILE *f = fopen(path, "w+b");
    if (!f)
        return BB_ERR_IO;
    BbStatus rc = statement_write_csv(acc, start, end, f, rows);
    rewind(f);
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    remove(path);
    return rc;
}

static void test_csv(void) {
    AccountList list;
    account_list_init(&list);
    Account *a = account_create(&list, "ivy", "fnv1a:0", 100);
    Account *b = account_create(&list, "jon", "fnv1a:0", 100);
    CHECK(a && b);
    if (!a || !b)
        return;
    time_t day = midnight(2025, 3, 14) + 9 * 3600 + 30 * 60; /* 09:30 local time */
    account_deposit(a, 150000, "Salary", day);
    account_withdraw(a, 1250, "-cash", day + 60);
    account_transfer(a, b, 50000, "Rent", day + 120);
    account_deposit(a, 99, "food & drink", midnight(2025, 3, 15) + 3600);

    char out[2048];
    unsigned long rows = 0;
    CHECK_INT_EQ(export(a, 0, 0, out, sizeof out, &rows), BB_OK);
    CHECK_INT_EQ(rows, 4);
    /* Oldest first, signed amounts, CRLF, a defused leading minus in the category. */
    CHECK_STR_EQ(out, "id,date,type,category,counterparty,amount,balance\r\n"
                      "1,2025-03-14 09:30:00,Deposit,Salary,,1500.00,1500.00\r\n"
                      "2,2025-03-14 09:31:00,Withdrawal,'-cash,,-12.50,1487.50\r\n"
                      "3,2025-03-14 09:32:00,Transfer out,Rent,100002,-500.00,987.50\r\n"
                      "4,2025-03-15 01:00:00,Deposit,food & drink,,0.99,988.49\r\n");

    time_t start, end;
    CHECK_INT_EQ(statement_parse_range("2025-03-15", "2025-03-15", &start, &end), BB_OK);
    CHECK_INT_EQ(export(a, start, end, out, sizeof out, &rows), BB_OK);
    CHECK_INT_EQ(rows, 1);
    CHECK(strstr(out, "food & drink") != NULL);
    CHECK(strstr(out, "Salary") == NULL);

    CHECK_INT_EQ(export(b, 0, 0, out, sizeof out, &rows), BB_OK);
    CHECK(strstr(out, "1,2025-03-14 09:32:00,Transfer in,Rent,100001,500.00,500.00\r\n") != NULL);

    /* Nothing in range: just the header. */
    CHECK_INT_EQ(statement_parse_range("2030-01-01", "", &start, &end), BB_OK);
    CHECK_INT_EQ(export(a, start, end, out, sizeof out, &rows), BB_OK);
    CHECK_INT_EQ(rows, 0);
    CHECK_STR_EQ(out, "id,date,type,category,counterparty,amount,balance\r\n");
    account_free_all(&list);
}

static void test_type_names(void) {
    CHECK_STR_EQ(tx_type_name(TX_DEPOSIT), "Deposit");
    CHECK_STR_EQ(tx_type_name(TX_TRANSFER_IN), "Transfer in");
    CHECK_STR_EQ(tx_type_name(TX_TRANSFER_OUT), "Transfer out");
}

void suite_statement(void) {
    test_parse_range();
    test_includes();
    test_csv();
    test_type_names();
}
