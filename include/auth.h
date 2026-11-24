#ifndef AUTH_H
#define AUTH_H

#include <time.h>

#include "account.h"

#define PIN_MIN_LEN         4
#define PIN_MAX_LEN         8
#define MAX_FAILED_ATTEMPTS 3
#define LOCKOUT_SECONDS     60

unsigned int hash_pin(const char *pin);
int validate_pin_format(const char *pin);
int verify_pin(Account *acc, const char *pin, time_t now);
void reset_lock(Account *acc);

#endif /* AUTH_H */
