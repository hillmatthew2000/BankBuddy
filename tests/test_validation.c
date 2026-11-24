#include <string.h>

#include "account.h"
#include "check.h"
#include "validation.h"

static void test_read_line(void) {
    FILE *in = test_input(" hello \r\n"
                          "0123456789ABCDEF\n" /* longer than the buffer below */
                          "next\n"
                          "12345678\n" /* exactly fills the buffer */
                          "\n"
                          "last line without newline");
    CHECK(in != NULL);
    if (!in)
        return;
    char buf[9];
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_OK);
    CHECK_STR_EQ(buf, "hello");
    /* The over-long line is reported and fully consumed... */
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_TOO_LONG);
    /* ...so its tail does not leak into the next read. */
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_OK);
    CHECK_STR_EQ(buf, "next");
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_OK);
    CHECK_STR_EQ(buf, "12345678");
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_OK);
    CHECK_STR_EQ(buf, "");
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_TOO_LONG);
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_EOF);
    CHECK_INT_EQ(read_line(in, buf, sizeof buf), LINE_EOF);
    fclose(in);
}

static void test_parse_ulong(void) {
    unsigned long v = 0;
    CHECK(parse_ulong("100001", &v));
    CHECK_INT_EQ(v, 100001);
    CHECK(parse_ulong("0", &v));
    CHECK(!parse_ulong("", &v));
    CHECK(!parse_ulong("12abc", &v));
    CHECK(!parse_ulong("abc", &v));
    /* strtoul would accept all of these; account numbers must not. */
    CHECK(!parse_ulong("-1", &v));
    CHECK(!parse_ulong("+1", &v));
    CHECK(!parse_ulong(" 1", &v));
    CHECK(!parse_ulong("99999999999999999999999", &v));
}

static void test_trim(void) {
    char s[32] = "  hello world \t";
    trim(s);
    CHECK_STR_EQ(s, "hello world");

    char empty[4] = "   ";
    trim(empty);
    CHECK_STR_EQ(empty, "");
}

static void test_username(void) {
    CHECK_INT_EQ(validate_username("demo"), BB_OK);
    CHECK_INT_EQ(validate_username("Mary-Jane O'Neil"), BB_OK);
    CHECK_INT_EQ(validate_username("j.smith_2"), BB_OK);
    CHECK_INT_EQ(validate_username(""), BB_ERR_RANGE);
    CHECK_INT_EQ(validate_username(" lead"), BB_ERR_PARSE);
    CHECK_INT_EQ(validate_username("-dash"), BB_ERR_PARSE);
    /* '|' is the data file's field separator and must never get in. */
    CHECK_INT_EQ(validate_username("bad|name"), BB_ERR_PARSE);
    CHECK_INT_EQ(validate_username("tab\there"), BB_ERR_PARSE);
    char long_name[USERNAME_MAX + 2];
    memset(long_name, 'a', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    CHECK_INT_EQ(validate_username(long_name), BB_ERR_RANGE);
    long_name[USERNAME_MAX] = '\0';
    CHECK_INT_EQ(validate_username(long_name), BB_OK);
}

static void test_names_match(void) {
    CHECK(names_match("Alice Smith", "Alice Smith"));
    CHECK(names_match("Alice Smith", "alice SMITH"));
    CHECK(!names_match("Alice Smith", "Alice"));
    CHECK(!names_match("Alice", "Alice Smith"));
    CHECK(!names_match("Alice Smith", "Alice  Smith"));
    CHECK(!names_match("demo", ""));
    CHECK(!names_match(NULL, "demo"));
}

static void test_sanitize_category(void) {
    char a[40] = "  Groceries ";
    sanitize_category(a);
    CHECK_STR_EQ(a, "Groceries");

    char b[40] = "food & drink";
    sanitize_category(b);
    CHECK_STR_EQ(b, "food & drink");

    char c[40] = "   ";
    sanitize_category(c);
    CHECK_STR_EQ(c, "General");

    char d[40] = "a|b\tc";
    sanitize_category(d);
    CHECK_STR_EQ(d, "a_b_c");
}

void suite_validation(void) {
    test_read_line();
    test_parse_ulong();
    test_trim();
    test_username();
    test_names_match();
    test_sanitize_category();
}
