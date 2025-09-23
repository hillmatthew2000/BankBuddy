#ifndef INTEREST_H
#define INTEREST_H

#include "account.h"

int apply_interest(Account *acc, double annual_rate, unsigned int periods_per_year, time_t now);

#endif /* INTEREST_H */
