#include "report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Finds the summary for `category`, appending a new one so first-seen order is kept. */
static CategorySummary *find_or_add(CategorySummary **head, const char *category) {
    CategorySummary **link = head;
    for (; *link; link = &(*link)->next)
        if (strcmp((*link)->category, category) == 0)
            return *link;
    CategorySummary *summary = calloc(1, sizeof *summary);
    if (!summary)
        return NULL;
    size_t len = strlen(category);
    if (len > CATEGORY_MAX)
        len = CATEGORY_MAX;
    memcpy(summary->category, category, len);
    summary->category[len] = '\0';
    *link = summary;
    return summary;
}

CategorySummary *generate_category_report(const Account *acc, time_t start_time, time_t end_time,
                                          BbStatus *status) {
    if (status)
        *status = BB_OK;
    if (!acc)
        return NULL;
    CategorySummary *head = NULL;
    for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
        if (start_time && tx->timestamp < start_time)
            continue;
        if (end_time && tx->timestamp > end_time)
            continue;
        CategorySummary *summary = find_or_add(&head, tx->category);
        if (!summary) {
            free_category_summaries(head);
            if (status)
                *status = BB_ERR_NO_MEMORY;
            return NULL;
        }
        /* Totals are bounded by the number of transactions times MONEY_MAX: no overflow. */
        if (tx_is_credit(tx->type))
            summary->credits += tx->amount;
        else
            summary->debits += tx->amount;
    }
    return head;
}

void print_category_report(const CategorySummary *head) {
    printf("%-20s %16s %16s %16s\n", "Category", "Credits", "Debits", "Net");
    printf("-----------------------------------------------------------------------\n");
    for (const CategorySummary *c = head; c; c = c->next) {
        char credits[MONEY_STR_MAX], debits[MONEY_STR_MAX], net[MONEY_STR_MAX];
        money_format(c->credits, credits, sizeof credits);
        money_format(c->debits, debits, sizeof debits);
        money_format(c->credits - c->debits, net, sizeof net);
        printf("%-20s %16s %16s %16s\n", c->category, credits, debits, net);
    }
}

void free_category_summaries(CategorySummary *head) {
    while (head) {
        CategorySummary *next = head->next;
        free(head);
        head = next;
    }
}
