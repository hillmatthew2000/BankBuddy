#include "account.h"
#include "check.h"
#include "interest.h"

static void test_create_and_find(void) {
    AccountList list;
    account_list_init(&list);
    Account *a = account_create(&list, "alice", "fnv1a:0", 1000);
    Account *b = account_create(&list, "bob", "fnv1a:0", 1000);
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
    Account *acc = account_create(&list, "carol", "fnv1a:0", 1000);
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
    Account *acc = account_create(&list, "dave", "fnv1a:0", 1000);
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

static void test_transfer(void) {
    AccountList list;
    account_list_init(&list);
    Account *a = account_create(&list, "erin", "fnv1a:0", 1000);
    Account *b = account_create(&list, "frank", "fnv1a:0", 1000);
    CHECK(a && b);
    if (!a || !b)
        return;
    CHECK_INT_EQ(account_deposit(a, 10000, "Salary", 1001), BB_OK);

    CHECK_INT_EQ(account_transfer(a, b, 2500, "Rent", 1002), BB_OK);
    CHECK_INT_EQ(a->balance, 7500);
    CHECK_INT_EQ(b->balance, 2500);
    const Transaction *out = a->tx_head, *in = b->tx_head;
    CHECK(out && in);
    if (out && in) {
        CHECK_INT_EQ(out->type, TX_TRANSFER_OUT);
        CHECK_INT_EQ(out->amount, 2500);
        CHECK_INT_EQ(out->balance_after, 7500);
        CHECK_INT_EQ(out->counterparty, b->account_number);
        CHECK_STR_EQ(out->category, "Rent");
        CHECK_INT_EQ(in->type, TX_TRANSFER_IN);
        CHECK_INT_EQ(in->balance_after, 2500);
        CHECK_INT_EQ(in->counterparty, a->account_number);
        CHECK_INT_EQ(in->tx_id, 1);
        CHECK(!tx_is_credit(out->type));
        CHECK(tx_is_credit(in->type));
    }

    /* Refused transfers change neither account. */
    CHECK_INT_EQ(account_transfer(a, b, 7501, "", 1003), BB_ERR_INSUFFICIENT);
    CHECK_INT_EQ(account_transfer(a, a, 1, "", 1003), BB_ERR_INVALID_ARG);
    CHECK_INT_EQ(account_transfer(a, b, 0, "", 1003), BB_ERR_RANGE);
    CHECK_INT_EQ(account_deposit(b, MONEY_MAX - 2500, "Lottery", 1003), BB_OK);
    CHECK_INT_EQ(account_transfer(a, b, 1, "", 1004), BB_ERR_RANGE); /* b is full */
    CHECK_INT_EQ(a->balance, 7500);
    CHECK_INT_EQ(a->next_tx_id, 3);
    CHECK_INT_EQ(b->balance, MONEY_MAX);
    account_free_all(&list);
}

static void test_close(void) {
    AccountList list;
    account_list_init(&list);
    Account *a = account_create(&list, "gina", "fnv1a:0", 1000);
    Account *b = account_create(&list, "hal", "fnv1a:0", 1000);
    CHECK(a && b);
    if (!a || !b)
        return;
    CHECK_INT_EQ(account_deposit(a, 100, "", 1001), BB_OK);
    CHECK_INT_EQ(account_close(a, 2000), BB_ERR_NOT_EMPTY);
    CHECK_INT_EQ(a->closed_at, 0);
    CHECK_INT_EQ(account_transfer(a, b, 100, "", 1002), BB_OK);
    CHECK_INT_EQ(account_close(a, 2000), BB_OK);
    CHECK_INT_EQ(a->closed_at, 2000);
    CHECK_INT_EQ(account_close(a, 2001), BB_ERR_CLOSED);

    /* A closed account takes part in nothing, but keeps its history. */
    Money credited;
    CHECK_INT_EQ(account_deposit(a, 1, "", 2002), BB_ERR_CLOSED);
    CHECK_INT_EQ(account_transfer(b, a, 1, "", 2002), BB_ERR_CLOSED);
    CHECK_INT_EQ(account_transfer(a, b, 1, "", 2002), BB_ERR_CLOSED);
    CHECK_INT_EQ(apply_interest(a, 500, 12, 2002, &credited), BB_ERR_CLOSED);
    CHECK(a->tx_head != NULL);
    CHECK_INT_EQ(b->balance, 100);
    account_free_all(&list);
}

void suite_account(void) {
    test_create_and_find();
    test_deposit_withdraw();
    test_apply_interest();
    test_transfer();
    test_close();
}
