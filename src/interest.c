#include <time.h>
#include "interest.h"
#include "account.h"
#include "validation.h"

int apply_interest(Account *acc, double annual_rate, unsigned int periods_per_year, time_t now) {
    if (!acc || periods_per_year == 0) return -1;
    double period_rate = annual_rate / (double)periods_per_year;
    double interest = acc->balance * period_rate;
    acc->balance += interest;
    acc->last_interest_applied = now;
    account_record_transaction(acc, 'I', interest, "Interest", acc->balance);
    return 0;
}
