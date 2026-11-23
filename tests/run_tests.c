#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"

int check_failures = 0;
int check_count = 0;

const char *test_temp_dir(void) {
    const char *names[] = {"TMPDIR", "TEMP", "TMP"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *dir = getenv(names[i]);
        if (dir && *dir)
            return dir;
    }
    return ".";
}

void test_temp_path(char *buf, size_t size, const char *name) {
    snprintf(buf, size, "%s/bankbuddy-test-%s", test_temp_dir(), name);
}

FILE *test_input(const char *text) {
    char path[512];
    test_temp_path(path, sizeof path, "input.txt");
    FILE *f = fopen(path, "wb");
    if (!f)
        return NULL;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    f = fopen(path, "rb");
    remove(path); /* POSIX keeps the open file; Windows refuses, so it is reused next time */
    return f;
}

void check_fail(const char *file, int line, const char *msg) {
    check_failures++;
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, msg);
}

/* Formats without printf: some C runtimes (old MinGW) lack a usable %lld. */
static const char *ll_to_str(long long v, char buf[24]) {
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

void check_fail_int(const char *file, int line, const char *expr, long long actual,
                    long long expected) {
    char a[24], e[24], msg[256];
    snprintf(msg, sizeof msg, "%s == %s, expected %s", expr, ll_to_str(actual, a),
             ll_to_str(expected, e));
    check_fail(file, line, msg);
}

typedef struct Suite {
    const char *name;
    void (*run)(void);
} Suite;

static const Suite suites[] = {
    {"validation", suite_validation}, {"auth", suite_auth},       {"report", suite_report},
    {"money", suite_money},           {"account", suite_account}, {"storage", suite_storage},
};

int main(void) {
    for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++) {
        int before = check_failures;
        suites[i].run();
        printf("%-12s %s\n", suites[i].name, check_failures == before ? "ok" : "FAILED");
    }
    printf("%d checks, %d failed\n", check_count, check_failures);
    return check_failures == 0 ? 0 : 1;
}
