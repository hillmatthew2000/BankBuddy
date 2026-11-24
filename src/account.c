#include "account.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_CATEGORY "General"

static void copy_string(char *dst, size_t size, const char *src) {
    size_t len = strlen(src);
    if (len >= size)
        len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void account_list_init(AccountList *list) {
    if (!list)
        return;
    list->head = NULL;
    list->last_account_number = FIRST_ACCOUNT_NUMBER;
}

static void free_transactions(Transaction *tx) {
    while (tx) {
        Transaction *older = tx->next;
        free(tx);
        tx = older;
    }
}

void account_free_all(AccountList *list) {
    if (!list)
        return;
    Account *acc = list->head;
    while (acc) {
        Account *next = acc->next;
        free_transactions(acc->tx_head);
        free(acc);
        acc = next;
    }
    list->head = NULL;
}

Account *account_create(AccountList *list, const char *username, const char *pin_credential,
                        time_t now) {
    if (!list || !username || !pin_credential)
        return NULL;
    Account *acc = calloc(1, sizeof *acc);
    if (!acc)
        return NULL;
    acc->account_number = ++list->last_account_number;
    copy_string(acc->username, sizeof acc->username, username);
    copy_string(acc->pin_credential, sizeof acc->pin_credential, pin_credential);
    acc->balance = 0;
    acc->created_at = now;
    acc->last_interest_applied = now;
    acc->next_tx_id = 1;
    acc->next = list->head;
    list->head = acc;
    return acc;
}

Account *account_find(AccountList *list, unsigned long account_number) {
    if (!list)
        return NULL;
    for (Account *acc = list->head; acc; acc = acc->next)
        if (acc->account_number == account_number)
            return acc;
    return NULL;
}

int tx_type_valid(int type) {
    return type == TX_DEPOSIT || type == TX_WITHDRAWAL || type == TX_INTEREST;
}

int tx_is_credit(TxType type) {
    return type == TX_DEPOSIT || type == TX_INTEREST;
}

BbStatus account_record_transaction(Account *acc, TxType type, Money amount, const char *category,
                                    Money balance_after, time_t now) {
    if (!acc || !tx_type_valid((int)type))
        return BB_ERR_INVALID_ARG;
    Transaction *tx = calloc(1, sizeof *tx);
    if (!tx)
        return BB_ERR_NO_MEMORY;
    tx->tx_id = acc->next_tx_id++;
    tx->type = type;
    tx->amount = amount;
    tx->balance_after = balance_after;
    tx->timestamp = now;
    copy_string(tx->category, sizeof tx->category,
                category && category[0] ? category : DEFAULT_CATEGORY);
    tx->next = acc->tx_head;
    acc->tx_head = tx;
    return BB_OK;
}

/* Moves `delta` (signed) into the balance and records it, or changes nothing on failure. */
static BbStatus apply(Account *acc, TxType type, Money amount, Money delta, const char *category,
                      time_t now) {
    Money new_balance;
    BbStatus rc = money_add(acc->balance, delta, &new_balance);
    if (rc != BB_OK)
        return rc;
    if (new_balance < 0)
        return BB_ERR_INSUFFICIENT;
    rc = account_record_transaction(acc, type, amount, category, new_balance, now);
    if (rc != BB_OK)
        return rc;
    acc->balance = new_balance;
    return BB_OK;
}

static BbStatus check_amount(const Account *acc, Money amount) {
    if (!acc)
        return BB_ERR_INVALID_ARG;
    if (amount <= 0 || amount > MONEY_MAX)
        return BB_ERR_RANGE;
    return BB_OK;
}

BbStatus account_deposit(Account *acc, Money amount, const char *category, time_t now) {
    BbStatus rc = check_amount(acc, amount);
    if (rc != BB_OK)
        return rc;
    return apply(acc, TX_DEPOSIT, amount, amount, category, now);
}

BbStatus account_withdraw(Account *acc, Money amount, const char *category, time_t now) {
    BbStatus rc = check_amount(acc, amount);
    if (rc != BB_OK)
        return rc;
    return apply(acc, TX_WITHDRAWAL, amount, -amount, category, now);
}
