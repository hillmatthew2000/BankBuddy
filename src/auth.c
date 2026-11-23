#include "auth.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

unsigned int hash_pin(const char *pin) {
    /* FNV-1a 32-bit */
    const unsigned int FNV_PRIME = 16777619u;
    unsigned int hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)pin; *p; ++p) {
        hash ^= *p;
        hash *= FNV_PRIME;
    }
    return hash;
}

int validate_pin_format(const char *pin) {
    size_t len = strlen(pin);
    if (len < PIN_MIN_LEN || len > PIN_MAX_LEN)
        return 0;
    for (size_t i = 0; i < len; ++i)
        if (!isdigit((unsigned char)pin[i]))
            return 0;
    return 1;
}

void reset_lock(Account *acc) {
    if (!acc)
        return;
    acc->failed_attempts = 0;
    acc->lock_until = 0;
}

int verify_pin(Account *acc, const char *pin, time_t now) {
    if (!acc || !pin)
        return 0;
    if (acc->lock_until && now < acc->lock_until)
        return 0; /* locked */
    unsigned int h = hash_pin(pin);
    if (h == acc->pin_hash) {
        reset_lock(acc);
        return 1;
    }
    acc->failed_attempts++;
    if (acc->failed_attempts >= MAX_FAILED_ATTEMPTS) {
        acc->lock_until = now + LOCKOUT_SECONDS;
        acc->failed_attempts = 0; /* reset after locking */
    }
    return 0;
}
