/* Minimal unit-test helpers: no dependencies, one binary, plain output. */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>

extern int check_failures;
extern int check_count;

void check_fail(const char *file, int line, const char *msg);
void check_fail_int(const char *file, int line, const char *expr, long long actual,
                    long long expected);
void check_fail_str(const char *file, int line, const char *expr, const char *actual,
                    const char *expected);

#define CHECK(cond)                                \
    do {                                           \
        check_count++;                             \
        if (!(cond))                               \
            check_fail(__FILE__, __LINE__, #cond); \
    } while (0)

#define CHECK_INT_EQ(actual, expected)                                       \
    do {                                                                     \
        check_count++;                                                       \
        if ((long long)(actual) != (long long)(expected))                    \
            check_fail_int(__FILE__, __LINE__, #actual, (long long)(actual), \
                           (long long)(expected));                           \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                       \
    do {                                                                     \
        const char *check_a_ = (actual);                                     \
        const char *check_e_ = (expected);                                   \
        check_count++;                                                       \
        if (check_a_ == NULL || strcmp(check_a_, check_e_) != 0)             \
            check_fail_str(__FILE__, __LINE__, #actual, check_a_, check_e_); \
    } while (0)

/* Scratch files live in $TMPDIR/$TEMP (or the current directory if neither is set). */
const char *test_temp_dir(void);
void test_temp_path(char *buf, size_t size, const char *name);
/* A readable stream holding exactly `text` (binary mode, so CRLF is preserved). */
FILE *test_input(const char *text);

/* Each test file exposes one suite function, listed in run_tests.c. */
void suite_validation(void);
void suite_auth(void);
void suite_report(void);
void suite_money(void);
void suite_account(void);
void suite_storage(void);
void suite_sha256(void);
void suite_term(void);
void suite_statement(void);

#endif /* CHECK_H */
