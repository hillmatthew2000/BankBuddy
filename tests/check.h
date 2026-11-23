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

#define CHECK_STR_EQ(actual, expected)                                                        \
    do {                                                                                      \
        const char *check_a_ = (actual);                                                      \
        const char *check_e_ = (expected);                                                    \
        check_count++;                                                                        \
        if (check_a_ == NULL || strcmp(check_a_, check_e_) != 0) {                            \
            char check_msg_[256];                                                             \
            snprintf(check_msg_, sizeof check_msg_, "%s == \"%s\", expected \"%s\"", #actual, \
                     check_a_ ? check_a_ : "(null)", check_e_);                               \
            check_fail(__FILE__, __LINE__, check_msg_);                                       \
        }                                                                                     \
    } while (0)

/* Each test file exposes one suite function, listed in run_tests.c. */
void suite_validation(void);
void suite_auth(void);
void suite_report(void);

#endif /* CHECK_H */
