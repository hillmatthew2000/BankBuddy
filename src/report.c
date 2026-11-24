#include "report.h"

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

void free_category_summaries(CategorySummary *head) {
    while (head) {
        CategorySummary *next = head->next;
        free(head);
        head = next;
    }
}
