#include <string.h>

#include "auth.h"
#include "check.h"

/* Few iterations keep the tests fast; the scheme is the same at any count. */
#define TEST_ITERATIONS 10u

/* FNV-1a("1234") as written by releases before PBKDF2. */
#define LEGACY_1234 "fnv1a:4257489661"

static void test_pin_format(void) {
    CHECK(validate_pin_format("1234"));
    CHECK(validate_pin_format("12345678"));
    CHECK(!validate_pin_format("123"));
    CHECK(!validate_pin_format("123456789"));
    CHECK(!validate_pin_format("12a4"));
    CHECK(!validate_pin_format(""));
}

static void test_credentials(void) {
    char a[PIN_CREDENTIAL_MAX + 1], b[PIN_CREDENTIAL_MAX + 1];
    CHECK_INT_EQ(pin_credential_create("2468", TEST_ITERATIONS, a, sizeof a), BB_OK);
    CHECK_INT_EQ(pin_credential_create("2468", TEST_ITERATIONS, b, sizeof b), BB_OK);
    CHECK(strncmp(a, "pbkdf2-sha256:10:", 17) == 0);
    CHECK_INT_EQ(strlen(a), 17 + 2 * PIN_SALT_SIZE + 1 + 64);
    CHECK(strcmp(a, b) != 0); /* a fresh salt every time */
    CHECK(pin_credential_valid(a));
    CHECK(pin_credential_matches(a, "2468"));
    CHECK(pin_credential_matches(b, "2468"));
    CHECK(!pin_credential_matches(a, "2469"));
    CHECK(!pin_credential_matches(a, ""));
    CHECK(!pin_credential_is_current(a)); /* too few iterations */

    /* Known answer: PBKDF2-HMAC-SHA256("1234", salt 00 01 .. 0f, 1000 iterations). */
    const char *known = "pbkdf2-sha256:1000:000102030405060708090a0b0c0d0e0f:"
                        "2240ea2a22754a3c4610356ef8e4d7acf25af2ac3d2d11866de97a8b7812c52c";
    CHECK(pin_credential_valid(known));
    CHECK(pin_credential_matches(known, "1234"));
    CHECK(!pin_credential_matches(known, "1235"));

    CHECK(pin_credential_valid(LEGACY_1234));
    CHECK(pin_credential_matches(LEGACY_1234, "1234"));
    CHECK(!pin_credential_matches(LEGACY_1234, "4321"));
    CHECK(!pin_credential_is_current(LEGACY_1234));

    char too_small[PIN_CREDENTIAL_MAX];
    CHECK_INT_EQ(pin_credential_create("1", 1, too_small, sizeof too_small), BB_ERR_INVALID_ARG);
    CHECK_INT_EQ(pin_credential_create("1", 0, a, sizeof a), BB_ERR_INVALID_ARG);
}

static void test_malformed_credentials(void) {
    const char *bad[] = {
        "",
        "1234",
        "fnv1a:",
        "fnv1a:-1",
        "fnv1a:4294967296", /* more than 32 bits */
        "fnv1a:12x",
        "pbkdf2-sha256:",
        "pbkdf2-sha256:0:000102030405060708090a0b0c0d0e0f:"
        "0000000000000000000000000000000000000000000000000000000000000000",
        "pbkdf2-sha256:10:000102030405060708090a0b0c0d0e:" /* short salt */
        "0000000000000000000000000000000000000000000000000000000000000000",
        "pbkdf2-sha256:10:000102030405060708090A0B0C0D0E0F:" /* upper case */
        "0000000000000000000000000000000000000000000000000000000000000000",
        "pbkdf2-sha256:10:000102030405060708090a0b0c0d0e0f:"
        "00000000000000000000000000000000000000000000000000000000000000", /* short hash */
        "pbkdf2-sha256:10:000102030405060708090a0b0c0d0e0f:"
        "0000000000000000000000000000000000000000000000000000000000000000:",
        "sha1:abc",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!pin_credential_valid(bad[i]));
        CHECK(!pin_credential_matches(bad[i], "1234"));
    }
}

static void test_lockout(void) {
    Account acc;
    memset(&acc, 0, sizeof acc);
    CHECK_INT_EQ(pin_credential_create("2468", TEST_ITERATIONS, acc.pin_credential,
                                       sizeof acc.pin_credential),
                 BB_OK);
    time_t now = 1000;

    CHECK_INT_EQ(verify_pin(&acc, "2468", now), AUTH_OK);
    for (int i = 0; i < MAX_FAILED_ATTEMPTS; i++)
        CHECK_INT_EQ(verify_pin(&acc, "0000", now), AUTH_WRONG_PIN);
    /* Locked: even the right PIN is refused until the lockout expires. */
    CHECK_INT_EQ(auth_lock_remaining(&acc, now + 1), LOCKOUT_SECONDS - 1);
    CHECK_INT_EQ(verify_pin(&acc, "2468", now + 1), AUTH_LOCKED);
    CHECK_INT_EQ(auth_lock_remaining(&acc, now + LOCKOUT_SECONDS), 0);
    CHECK_INT_EQ(verify_pin(&acc, "2468", now + LOCKOUT_SECONDS), AUTH_OK);
    CHECK_INT_EQ(acc.failed_attempts, 0);
    CHECK_INT_EQ(acc.lock_until, 0);

    /* A lockout that ends implausibly far ahead (the clock was turned back) is capped. */
    acc.lock_until = now + 100000;
    CHECK_INT_EQ(verify_pin(&acc, "2468", now), AUTH_LOCKED);
    CHECK_INT_EQ(acc.lock_until, now + LOCKOUT_SECONDS);
}

/* A legacy credential is replaced by a current one on the first successful login. */
static void test_upgrade_on_login(void) {
    Account acc;
    memset(&acc, 0, sizeof acc);
    strcpy(acc.pin_credential, LEGACY_1234);

    CHECK_INT_EQ(verify_pin(&acc, "9999", 1000), AUTH_WRONG_PIN);
    CHECK_STR_EQ(acc.pin_credential, LEGACY_1234); /* not on a failed attempt */

    CHECK_INT_EQ(verify_pin(&acc, "1234", 1000), AUTH_OK);
    CHECK(pin_credential_is_current(acc.pin_credential));
    CHECK(pin_credential_matches(acc.pin_credential, "1234"));
    CHECK(!pin_credential_matches(acc.pin_credential, "9999"));
}

void suite_auth(void) {
    test_pin_format();
    test_credentials();
    test_malformed_credentials();
    test_lockout();
    test_upgrade_on_login();
}
