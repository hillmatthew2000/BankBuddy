#ifndef REPORT_H
#define REPORT_H

#include <time.h>

#include "account.h"

typedef struct CategorySummary {
    char category[CATEGORY_MAX + 1];
    Money credits; /* deposits and interest */
    Money debits;  /* withdrawals */
    struct CategorySummary *next;
} CategorySummary;

/*
 * Totals per category for transactions in [start_time, end_time]; a zero bound is open.
 * Returns NULL for an account with no matching transactions. *status (optional) receives
 * BB_ERR_NO_MEMORY if the report could not be built.
 */
CategorySummary *generate_category_report(const Account *acc, time_t start_time, time_t end_time,
                                          BbStatus *status);
void free_category_summaries(CategorySummary *head);
void print_category_report(const CategorySummary *head);

#endif /* REPORT_H */
