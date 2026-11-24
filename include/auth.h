#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "account.h"

#define PIN_MIN_LEN         4
#define PIN_MAX_LEN         8
#define MAX_FAILED_ATTEMPTS 3
#define LOCKOUT_SECONDS     60

/*
 * PINs are stored as credentials of the form
 *
 *   pbkdf2-sha256:<iterations>:<salt, 32 hex digits>:<hash, 64 hex digits>
 *
 * with a random salt per account. Accounts created by older releases hold "fnv1a:<number>",
 * an unsalted 32-bit hash; those are accepted and replaced by the current scheme the next
 * time their owner logs in.
 */
#define PIN_HASH_ITERATIONS 600000u
#define PIN_SALT_SIZE       16

typedef enum AuthResult {
    AUTH_OK = 0,
    AUTH_WRONG_PIN, /* the attempt was counted towards the lockout */
    AUTH_LOCKED     /* too many failures; nothing was checked */
} AuthResult;

/* 1 if `pin` is PIN_MIN_LEN..PIN_MAX_LEN digits. */
int validate_pin_format(const char *pin);

/* Hashes `pin` with a fresh random salt. Fails with BB_ERR_IO if no random bytes are available. */
BbStatus pin_credential_create(const char *pin, uint32_t iterations, char *out, size_t size);

/* 1 if `credential` is well formed in any supported scheme. */
int pin_credential_valid(const char *credential);

/* 1 if `pin` matches. Comparison of the derived hash takes constant time. */
int pin_credential_matches(const char *credential, const char *pin);

/* 1 if the credential uses the current scheme with at least PIN_HASH_ITERATIONS. */
int pin_credential_is_current(const char *credential);

/*
 * Checks a login attempt and applies the lockout policy: MAX_FAILED_ATTEMPTS wrong PINs lock
 * the account for LOCKOUT_SECONDS. A successful login re-hashes an outdated credential with
 * the current scheme (if that fails the old one is kept). Callers should save afterwards.
 */
AuthResult verify_pin(Account *acc, const char *pin, time_t now);

/*
 * Spends the same time as checking a real credential, so that a login attempt for an account
 * that does not exist cannot be told apart by how long it takes.
 */
void auth_dummy_check(const char *pin);

/* Seconds until the lockout ends, or 0 if the account is not locked. */
long auth_lock_remaining(const Account *acc, time_t now);

void reset_lock(Account *acc);

#endif /* AUTH_H */
