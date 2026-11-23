#include "check.h"
#include "validation.h"

static void test_parse_ulong(void) {
    unsigned long v = 0;
    CHECK(parse_ulong("100001", &v));
    CHECK_INT_EQ(v, 100001);
    CHECK(!parse_ulong("", &v));
    CHECK(!parse_ulong("12abc", &v));
    CHECK(!parse_ulong("abc", &v));
}

static void test_trim(void) {
    char s[32] = "  hello world \t";
    trim(s);
    CHECK_STR_EQ(s, "hello world");

    char empty[4] = "   ";
    trim(empty);
    CHECK_STR_EQ(empty, "");
}

static void test_sanitize_category(void) {
    char a[40] = "  Groceries ";
    sanitize_category(a);
    CHECK_STR_EQ(a, "Groceries");

    char b[40] = "food & drink";
    sanitize_category(b);
    CHECK_STR_EQ(b, "food___drink");

    char c[40] = "   ";
    sanitize_category(c);
    CHECK_STR_EQ(c, "General");
}

void suite_validation(void) {
    test_parse_ulong();
    test_trim();
    test_sanitize_category();
}
