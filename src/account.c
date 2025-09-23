#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account.h"
#include "validation.h"

void account_list_init(AccountList *list) {
    if (!list) return;
    list->head = NULL;
    list->next_account_number = 100000; /* start range */
}

static Account *allocate_account(void) {
    Account *acc = (Account*)calloc(1, sizeof(Account));
    return acc;
}

Account *account_create(AccountList *list, const char *username, unsigned int pin_hash, double initial_balance) {
    if (!list || !username) return NULL;
    Account *acc = allocate_account();
    if (!acc) return NULL;
    acc->account_number = ++list->next_account_number;
    strncpy(acc->username, username, USERNAME_MAX);
    acc->username[USERNAME_MAX] = '\0';
    acc->pin_hash = pin_hash;
    acc->balance = initial_balance;
    acc->created_at = acc->last_interest_applied = time(NULL);
    acc->failed_attempts = 0;
    acc->lock_until = 0;
    acc->tx_head = NULL;
    acc->next = list->head;
    list->head = acc;
    return acc;
}

Account *account_find(AccountList *list, unsigned long account_number) {
    if (!list) return NULL;
    for (Account *cur = list->head; cur; cur = cur->next) {
        if (cur->account_number == account_number) return cur;
    }
    return NULL;
}

unsigned long account_next_tx_id(const Account *acc) {
    unsigned long max_id = 0;
    for (const Transaction *t = acc ? acc->tx_head : NULL; t; t = t->next) {
        if (t->tx_id > max_id) max_id = t->tx_id;
    }
    return max_id + 1;
}

int account_record_transaction(Account *acc, char type, double amount, const char *category, double new_balance) {
    if (!acc) return -1;
    Transaction *tx = (Transaction*)calloc(1, sizeof(Transaction));
    if (!tx) return -1;
    tx->tx_id = account_next_tx_id(acc);
    tx->amount = amount;
    tx->type = type;
    tx->timestamp = time(NULL);
    tx->balance_after = new_balance;
    if (category) {
        strncpy(tx->category, category, CATEGORY_MAX);
        tx->category[CATEGORY_MAX] = '\0';
    } else {
        strncpy(tx->category, "General", CATEGORY_MAX);
        tx->category[CATEGORY_MAX] = '\0';
    }
    tx->next = acc->tx_head;
    acc->tx_head = tx;
    return 0;
}

void account_free_all(AccountList *list) {
    if (!list) return;
    Account *cur = list->head;
    while (cur) {
        Transaction *t = cur->tx_head;
        while (t) {
            Transaction *nxtt = t->next;
            free(t);
            t = nxtt;
        }
        Account *nxt = cur->next;
        free(cur);
        cur = nxt;
    }
    list->head = NULL;
}
