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
    Account *acc = account_create(&list, "tester", 0, 0.0);
    CHECK(acc != NULL);
    if (!acc)
        return;
    account_record_transaction(acc, 'D', 100.0, "Salary", 100.0);
    account_record_transaction(acc, 'W', 30.0, "Food", 70.0);
    account_record_transaction(acc, 'W', 20.0, "Food", 50.0);

    CategorySummary *head = generate_category_report(acc, 0, 0);
    const CategorySummary *food = find(head, "Food");
    const CategorySummary *salary = find(head, "Salary");
    CHECK(food != NULL);
    CHECK(salary != NULL);
    if (food && salary) {
        CHECK(food->withdrawals == 50.0);
        CHECK(food->deposits == 0.0);
        CHECK(salary->deposits == 100.0);
    }
    free_category_summaries(head);
    account_free_all(&list);
}

void suite_report(void) {
    test_category_totals();
}
