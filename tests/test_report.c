#include <string.h>

#include "check.h"
#include "report.h"

static const CategorySummary *find(const CategorySummary *head, const char *name) {
    for (; head; head = head->next)
        if (strcmp(head->category, name) == 0)
            return head;
    return NULL;
}

static void test_category_totals(void) {
    AccountList list;
    account_list_init(&list);
    Account *acc = account_create(&list, "tester", "fnv1a:0", 100);
    CHECK(acc != NULL);
    if (!acc)
        return;
    account_deposit(acc, 10000, "Salary", 100);
    account_withdraw(acc, 3000, "Food", 200);
    account_withdraw(acc, 2000, "Food", 300);
    account_record_transaction(acc, TX_INTEREST, 25, "Interest", 5025, 400);

    BbStatus rc;
    CategorySummary *head = generate_category_report(acc, 0, 0, &rc);
    CHECK_INT_EQ(rc, BB_OK);
    const CategorySummary *food = find(head, "Food");
    const CategorySummary *salary = find(head, "Salary");
    const CategorySummary *interest = find(head, "Interest");
    CHECK(food && salary && interest);
    if (food && salary && interest) {
        CHECK_INT_EQ(food->debits, 5000);
        CHECK_INT_EQ(food->credits, 0);
        CHECK_INT_EQ(salary->credits, 10000);
        CHECK_INT_EQ(interest->credits, 25);
    }
    free_category_summaries(head);

    /* Time window: only the two Food withdrawals fall in [200, 300]. */
    head = generate_category_report(acc, 200, 300, &rc);
    CHECK(head != NULL && head->next == NULL);
    if (head)
        CHECK_INT_EQ(head->debits, 5000);
    free_category_summaries(head);
    account_free_all(&list);
}

void suite_report(void) {
    test_category_totals();
}
