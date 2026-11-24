#include "account.h"
#include "check.h"
#include "interest.h"

static void test_create_and_find(void) {
    AccountList list;
    account_list_init(&list);
    Account *a = account_create(&list, "alice", 0, 1000);
    Account *b = account_create(&list, "bob", 0, 1000);
    CHECK(a && b);
    if (!a || !b)
        return;
    CHECK_INT_EQ(a->account_number, FIRST_ACCOUNT_NUMBER + 1);
    CHECK_INT_EQ(b->account_number, FIRST_ACCOUNT_NUMBER + 2);
    CHECK(account_find(&list, a->account_number) == a);
    CHECK(account_find(&list, 42) == NULL);
    CHECK_INT_EQ(a->balance, 0);
    account_free_all(&list);
    CHECK(list.head == NULL);
}

static void test_deposit_withdraw(void) {
    AccountList list;
    account_list_init(&list);
    Account *acc = account_create(&list, "carol", 0, 1000);
    CHECK(acc != NULL);
    if (!acc)
        return;

    CHECK_INT_EQ(account_deposit(acc, 10000, "Salary", 1001), BB_OK);
    CHECK_INT_EQ(account_withdraw(acc, 2550, "", 1002), BB_OK);
    CHECK_INT_EQ(acc->balance, 7450);

    /* Newest first, sequential ids, empty category defaulted. */
    const Transaction *tx = acc->tx_head;
    CHECK(tx != NULL);
    if (tx) {
        CHECK_INT_EQ(tx->tx_id, 2);
        CHECK_INT_EQ(tx->type, TX_WITHDRAWAL);
        CHECK_INT_EQ(tx->amount, 2550);
        CHECK_INT_EQ(tx->balance_after, 7450);
        CHECK_STR_EQ(tx->category, "General");
        CHECK(tx->next && tx->next->tx_id == 1);
    }

    /* Failed operations leave the account untouched. */
    CHECK_INT_EQ(account_withdraw(acc, 7451, "Rent", 1003), BB_ERR_INSUFFICIENT);
    CHECK_INT_EQ(account_deposit(acc, 0, "Gift", 1003), BB_ERR_RANGE);
    CHECK_INT_EQ(account_deposit(acc, -5, "Gift", 1003), BB_ERR_RANGE);
    CHECK_INT_EQ(account_deposit(acc, MONEY_MAX, "Lottery", 1003), BB_ERR_RANGE);
    CHECK_INT_EQ(acc->balance, 7450);
    CHECK(acc->tx_head == tx);
    CHECK_INT_EQ(acc->next_tx_id, 3);

    /* Withdrawing everything is allowed. */
    CHECK_INT_EQ(account_withdraw(acc, 7450, "Rent", 1004), BB_OK);
    CHECK_INT_EQ(acc->balance, 0);
    account_free_all(&list);
}

static void test_apply_interest(void) {
    AccountList list;
    account_list_init(&list);
    Account *acc = account_create(&list, "dave", 0, 1000);
    CHECK(acc != NULL);
    if (!acc)
        return;
    Money credited = -1;
    CHECK_INT_EQ(apply_interest(acc, 500, 12, 2000, &credited), BB_OK);
    CHECK_INT_EQ(credited, 0);
    CHECK(acc->tx_head == NULL); /* nothing recorded for zero interest */

    CHECK_INT_EQ(account_deposit(acc, 120000, "Savings", 2001), BB_OK);
    CHECK_INT_EQ(apply_interest(acc, 500, 12, 2002, &credited), BB_OK);
    CHECK_INT_EQ(credited, 500);
    CHECK_INT_EQ(acc->balance, 120500);
    CHECK_INT_EQ(acc->last_interest_applied, 2002);
    CHECK(acc->tx_head && acc->tx_head->type == TX_INTEREST);
    account_free_all(&list);
}

void suite_account(void) {
    test_create_and_find();
    test_deposit_withdraw();
    test_apply_interest();
}
