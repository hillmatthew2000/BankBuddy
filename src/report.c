#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "report.h"

static CategorySummary *find_or_add(CategorySummary **head, const char *category) {
    for (CategorySummary *c = *head; c; c = c->next) {
        if (strcmp(c->category, category) == 0) return c;
    }
    CategorySummary *ns = (CategorySummary*)calloc(1, sizeof(CategorySummary));
    if (!ns) return NULL;
    strncpy(ns->category, category, CATEGORY_MAX);
    ns->category[CATEGORY_MAX] = '\0';
    ns->next = *head;
    *head = ns;
    return ns;
}

CategorySummary *generate_category_report(const Account *acc, time_t start_time, time_t end_time) {
    if (!acc) return NULL;
    CategorySummary *head = NULL;
    for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
        if (start_time && tx->timestamp < start_time) continue;
        if (end_time && tx->timestamp > end_time) continue;
        CategorySummary *cat = find_or_add(&head, tx->category);
        if (!cat) break;
        if (tx->type == 'D' || tx->type == 'I') cat->deposits += tx->amount;
        else if (tx->type == 'W') cat->withdrawals += tx->amount;
    }
    return head;
}

void print_category_report(const CategorySummary *head) {
    printf("Category             Deposits        Withdrawals     Net\n");
    printf("-----------------------------------------------------------\n");
    for (const CategorySummary *c = head; c; c = c->next) {
        double net = c->deposits - c->withdrawals;
        printf("%-20s %12.2f %14.2f %10.2f\n", c->category, c->deposits, c->withdrawals, net);
    }
}

void free_category_summaries(CategorySummary *head) {
    while (head) {
        CategorySummary *n = head->next;
        free(head);
        head = n;
    }
}
