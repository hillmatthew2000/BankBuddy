#include <string.h>

#include "auth.h"
#include "check.h"

static void test_pin_format(void) {
    CHECK(validate_pin_format("1234"));
    CHECK(validate_pin_format("12345678"));
    CHECK(!validate_pin_format("123"));
    CHECK(!validate_pin_format("123456789"));
    CHECK(!validate_pin_format("12a4"));
    CHECK(!validate_pin_format(""));
}

static void test_lockout(void) {
    Account acc;
    memset(&acc, 0, sizeof acc);
    acc.pin_hash = hash_pin("2468");
    time_t now = 1000;

    CHECK(verify_pin(&acc, "2468", now));
    for (int i = 0; i < MAX_FAILED_ATTEMPTS; i++)
        CHECK(!verify_pin(&acc, "0000", now));
    /* Locked: even the right PIN is refused until the lockout expires. */
    CHECK(!verify_pin(&acc, "2468", now + 1));
    CHECK(verify_pin(&acc, "2468", now + LOCKOUT_SECONDS));
    CHECK_INT_EQ(acc.failed_attempts, 0);
}

void suite_auth(void) {
    test_pin_format();
    test_lockout();
}
