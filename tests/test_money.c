#include "check.h"
#include "interest.h"
#include "money.h"

static Money parsed(const char *text) {
    Money m = -1;
    return money_parse(text, &m) == BB_OK ? m : -1;
}

static void test_parse_valid(void) {
    CHECK_INT_EQ(parsed("0"), 0);
    CHECK_INT_EQ(parsed("25"), 2500);
    CHECK_INT_EQ(parsed("25.5"), 2550);
    CHECK_INT_EQ(parsed("25.05"), 2505);
    CHECK_INT_EQ(parsed("0.01"), 1);
    CHECK_INT_EQ(parsed(".75"), 75);
    CHECK_INT_EQ(parsed("1,234.56"), 123456);
    CHECK_INT_EQ(parsed("1,000,000"), 100000000);
    CHECK_INT_EQ(parsed("1000000000"), MONEY_MAX);
    /* Values that binary floating point cannot hold exactly stay exact. */
    CHECK_INT_EQ(parsed("0.10") + parsed("0.20"), parsed("0.30"));
}

static void test_parse_invalid(void) {
    Money m;
    const char *bad[] = {"",     ".",       "25.",  "12.345", "-5",  "+5",  "1e3",
                         "nan",  "inf",     "0x10", "12a",    " 12", "12 ", ",100",
                         "1,00", "12,3456", "1..2", "1.2.3",  "$5"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        BbStatus rc = money_parse(bad[i], &m);
        if (rc != BB_ERR_PARSE)
            check_fail(__FILE__, __LINE__, bad[i]);
        check_count++;
    }
    CHECK_INT_EQ(money_parse("1000000000.01", &m), BB_ERR_RANGE);
    CHECK_INT_EQ(money_parse("99999999999999999999999", &m), BB_ERR_RANGE);
}

static void test_format(void) {
    char buf[MONEY_STR_MAX];
    money_format(0, buf, sizeof buf);
    CHECK_STR_EQ(buf, "0.00");
    money_format(7, buf, sizeof buf);
    CHECK_STR_EQ(buf, "0.07");
    money_format(-7, buf, sizeof buf);
    CHECK_STR_EQ(buf, "-0.07");
    money_format(123456, buf, sizeof buf);
    CHECK_STR_EQ(buf, "1,234.56");
    money_format(MONEY_MAX, buf, sizeof buf);
    CHECK_STR_EQ(buf, "1,000,000,000.00");
    money_format(INT64_MIN, buf, sizeof buf);
    CHECK_STR_EQ(buf, "-92,233,720,368,547,758.08");
    money_format_plain(-123456789, buf, sizeof buf);
    CHECK_STR_EQ(buf, "-1234567.89");

    char tiny[5];
    money_format(123456, tiny, sizeof tiny);
    CHECK_STR_EQ(tiny, "1,23");
}

static void test_add(void) {
    Money out = 0;
    CHECK_INT_EQ(money_add(150, -50, &out), BB_OK);
    CHECK_INT_EQ(out, 100);
    CHECK_INT_EQ(money_add(MONEY_MAX, 1, &out), BB_ERR_RANGE);
    CHECK_INT_EQ(money_add(INT64_MAX, 1, &out), BB_ERR_RANGE);
    CHECK_INT_EQ(money_add(INT64_MIN, -1, &out), BB_ERR_RANGE);
}

static void test_interest(void) {
    int bp = -1;
    CHECK_INT_EQ(interest_parse_rate("4.75", &bp), BB_OK);
    CHECK_INT_EQ(bp, 475);
    CHECK_INT_EQ(interest_parse_rate("100", &bp), BB_OK);
    CHECK_INT_EQ(bp, 10000);
    CHECK_INT_EQ(interest_parse_rate("100.01", &bp), BB_ERR_RANGE);
    CHECK_INT_EQ(interest_parse_rate("0.05x", &bp), BB_ERR_PARSE);

    Money interest = -1;
    /* 1,000.00 at 6% monthly: 5.00 exactly. */
    CHECK_INT_EQ(interest_for_period(100000, 600, 12, &interest), BB_OK);
    CHECK_INT_EQ(interest, 500);
    /* 512.35 at 5% monthly = 2.13479...: rounds to 2.13. */
    CHECK_INT_EQ(interest_for_period(51235, 500, 12, &interest), BB_OK);
    CHECK_INT_EQ(interest, 213);
    /* Half a cent rounds up: 1.00 at 6% over 12 periods = 0.5 cents. */
    CHECK_INT_EQ(interest_for_period(100, 600, 12, &interest), BB_OK);
    CHECK_INT_EQ(interest, 1);
    CHECK_INT_EQ(interest_for_period(MONEY_MAX, INTEREST_MAX_RATE_BP, 1, &interest), BB_OK);
    CHECK_INT_EQ(interest, MONEY_MAX);
    CHECK_INT_EQ(interest_for_period(100, 600, 0, &interest), BB_ERR_RANGE);
    CHECK_INT_EQ(interest_for_period(100, 600, INTEREST_MAX_PERIODS + 1, &interest), BB_ERR_RANGE);
    CHECK_INT_EQ(interest_for_period(100, INTEREST_MAX_RATE_BP + 1, 12, &interest), BB_ERR_RANGE);
}

void suite_money(void) {
    test_parse_valid();
    test_parse_invalid();
    test_format();
    test_add();
    test_interest();
}
