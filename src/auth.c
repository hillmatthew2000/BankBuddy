#include "auth.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "sha256.h"

#define SCHEME_PBKDF2  "pbkdf2-sha256:"
#define SCHEME_FNV1A   "fnv1a:"
#define MAX_ITERATIONS 100000000UL

int validate_pin_format(const char *pin) {
    size_t len = strlen(pin);
    if (len < PIN_MIN_LEN || len > PIN_MAX_LEN)
        return 0;
    for (size_t i = 0; i < len; ++i)
        if (!isdigit((unsigned char)pin[i]))
            return 0;
    return 1;
}

/* ---------- parsing ---------- */

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Parses decimal digits up to `stop` (not included); no sign, no overflow past `max`. */
static const char *parse_number(const char *s, char stop, unsigned long max, unsigned long *out) {
    unsigned long v = 0;
    const char *p = s;
    for (; *p >= '0' && *p <= '9'; p++) {
        unsigned d = (unsigned)(*p - '0');
        if (v > (max - d) / 10)
            return NULL;
        v = v * 10 + d;
    }
    if (p == s || *p != stop)
        return NULL;
    *out = v;
    return p;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* Decodes exactly 2 * len lowercase hex digits followed by `stop`. */
static const char *parse_hex(const char *s, char stop, unsigned char *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        int hi = hex_value(s[2 * i]);
        int lo = hi < 0 ? -1 : hex_value(s[2 * i + 1]);
        if (lo < 0)
            return NULL;
        out[i] = (unsigned char)(hi << 4 | lo);
    }
    return s[2 * len] == stop ? s + 2 * len : NULL;
}

static void put_hex(char *out, const unsigned char *bytes, size_t len) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * len] = '\0';
}

typedef struct Pbkdf2Credential {
    uint32_t iterations;
    unsigned char salt[PIN_SALT_SIZE];
    unsigned char hash[SHA256_DIGEST_SIZE];
} Pbkdf2Credential;

static int parse_pbkdf2(const char *s, Pbkdf2Credential *c) {
    unsigned long iterations;
    if (!starts_with(s, SCHEME_PBKDF2))
        return 0;
    s += strlen(SCHEME_PBKDF2);
    if (!(s = parse_number(s, ':', MAX_ITERATIONS, &iterations)) || iterations == 0 ||
        !(s = parse_hex(s + 1, ':', c->salt, sizeof c->salt)) ||
        !parse_hex(s + 1, '\0', c->hash, sizeof c->hash))
        return 0;
    c->iterations = (uint32_t)iterations;
    return 1;
}

static int parse_fnv1a(const char *s, uint32_t *hash) {
    unsigned long v;
    if (!starts_with(s, SCHEME_FNV1A) ||
        !parse_number(s + strlen(SCHEME_FNV1A), '\0', 0xFFFFFFFFUL, &v))
        return 0;
    *hash = (uint32_t)v;
    return 1;
}

/* The scheme used by releases before PBKDF2: unsalted 32-bit FNV-1a. Verification only. */
static uint32_t fnv1a(const char *pin) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)pin; *p; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

/* ---------- credentials ---------- */

BbStatus pin_credential_create(const char *pin, uint32_t iterations, char *out, size_t size) {
    if (!pin || !out || iterations == 0 || iterations > MAX_ITERATIONS ||
        size <= PIN_CREDENTIAL_MAX)
        return BB_ERR_INVALID_ARG;
    Pbkdf2Credential c;
    c.iterations = iterations;
    BbStatus rc = platform_random_bytes(c.salt, sizeof c.salt);
    if (rc != BB_OK)
        return rc;
    pbkdf2_hmac_sha256(pin, strlen(pin), c.salt, sizeof c.salt, iterations, c.hash, sizeof c.hash);

    char salt[2 * PIN_SALT_SIZE + 1], hash[2 * SHA256_DIGEST_SIZE + 1];
    put_hex(salt, c.salt, sizeof c.salt);
    put_hex(hash, c.hash, sizeof c.hash);
    snprintf(out, size, SCHEME_PBKDF2 "%lu:%s:%s", (unsigned long)iterations, salt, hash);
    secure_zero(&c, sizeof c);
    return BB_OK;
}

int pin_credential_valid(const char *credential) {
    Pbkdf2Credential c;
    uint32_t h;
    return credential && strlen(credential) <= PIN_CREDENTIAL_MAX &&
           (parse_pbkdf2(credential, &c) || parse_fnv1a(credential, &h));
}

int pin_credential_matches(const char *credential, const char *pin) {
    if (!credential || !pin)
        return 0;
    Pbkdf2Credential c;
    if (parse_pbkdf2(credential, &c)) {
        unsigned char derived[SHA256_DIGEST_SIZE];
        pbkdf2_hmac_sha256(pin, strlen(pin), c.salt, sizeof c.salt, c.iterations, derived,
                           sizeof derived);
        int match = bytes_equal_ct(derived, c.hash, sizeof derived);
        secure_zero(derived, sizeof derived);
        return match;
    }
    uint32_t stored;
    if (parse_fnv1a(credential, &stored))
        return fnv1a(pin) == stored;
    return 0;
}

int pin_credential_is_current(const char *credential) {
    Pbkdf2Credential c;
    return credential && parse_pbkdf2(credential, &c) && c.iterations >= PIN_HASH_ITERATIONS;
}

/* ---------- login policy ---------- */

void reset_lock(Account *acc) {
    if (!acc)
        return;
    acc->failed_attempts = 0;
    acc->lock_until = 0;
}

long auth_lock_remaining(const Account *acc, time_t now) {
    if (!acc || !acc->lock_until || now >= acc->lock_until)
        return 0;
    double left = difftime(acc->lock_until, now);
    return left > LOCKOUT_SECONDS ? LOCKOUT_SECONDS : (long)left;
}

AuthResult verify_pin(Account *acc, const char *pin, time_t now) {
    if (!acc || !pin)
        return AUTH_WRONG_PIN;
    if (acc->lock_until > now + LOCKOUT_SECONDS)
        acc->lock_until = now + LOCKOUT_SECONDS; /* the clock was turned back */
    /* The PIN is checked even while locked, so a locked account takes as long to answer as any
     * other and cannot be spotted by timing. */
    int match = pin_credential_matches(acc->pin_credential, pin);
    if (auth_lock_remaining(acc, now) > 0)
        return AUTH_LOCKED;
    if (match) {
        reset_lock(acc);
        if (!pin_credential_is_current(acc->pin_credential)) {
            char upgraded[PIN_CREDENTIAL_MAX + 1];
            if (pin_credential_create(pin, PIN_HASH_ITERATIONS, upgraded, sizeof upgraded) == BB_OK)
                memcpy(acc->pin_credential, upgraded, sizeof upgraded);
        }
        return AUTH_OK;
    }
    acc->failed_attempts++;
    if (acc->failed_attempts >= MAX_FAILED_ATTEMPTS) {
        acc->lock_until = now + LOCKOUT_SECONDS;
        acc->failed_attempts = 0; /* start counting afresh once the lockout ends */
    }
    return AUTH_WRONG_PIN;
}

void auth_dummy_check(const char *pin) {
    /* Same derivation as a real check; the result is irrelevant, only the work matters. */
    static const unsigned char salt[PIN_SALT_SIZE];
    unsigned char derived[SHA256_DIGEST_SIZE];
    if (!pin)
        pin = "";
    pbkdf2_hmac_sha256(pin, strlen(pin), salt, sizeof salt, PIN_HASH_ITERATIONS, derived,
                       sizeof derived);
    secure_zero(derived, sizeof derived);
}
