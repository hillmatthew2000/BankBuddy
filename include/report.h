#ifndef REPORT_H
#define REPORT_H

#include <time.h>

#include "account.h"

typedef struct CategorySummary {
    char category[CATEGORY_MAX + 1];
    double deposits;
    double withdrawals;
    struct CategorySummary *next;
} CategorySummary;

void free_category_summaries(CategorySummary *head);
CategorySummary *generate_category_report(const Account *acc, time_t start_time, time_t end_time);
void print_category_report(const CategorySummary *head);

#endif /* REPORT_H */
