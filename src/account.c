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
    return type == TX_DEPOSIT || type == TX_WITHDRAWAL || type == TX_INTEREST ||
           type == TX_TRANSFER_OUT || type == TX_TRANSFER_IN;
}

int tx_is_credit(TxType type) {
    return type == TX_DEPOSIT || type == TX_INTEREST || type == TX_TRANSFER_IN;
}

/* Builds a transaction record that is not yet linked into any account. */
static Transaction *new_transaction(TxType type, Money amount, const char *category,
                                    Money balance_after, unsigned long counterparty, time_t now) {
    Transaction *tx = calloc(1, sizeof *tx);
    if (!tx)
        return NULL;
    tx->type = type;
    tx->amount = amount;
    tx->balance_after = balance_after;
    tx->timestamp = now;
    tx->counterparty = counterparty;
    copy_string(tx->category, sizeof tx->category,
                category && category[0] ? category : DEFAULT_CATEGORY);
    return tx;
}

/* Makes `tx` the account's newest transaction and gives it the next id. */
static void link_transaction(Account *acc, Transaction *tx) {
    tx->tx_id = acc->next_tx_id++;
    tx->next = acc->tx_head;
    acc->tx_head = tx;
}

BbStatus account_record_transaction(Account *acc, TxType type, Money amount, const char *category,
                                    Money balance_after, time_t now) {
    if (!acc || !tx_type_valid((int)type))
        return BB_ERR_INVALID_ARG;
    Transaction *tx = new_transaction(type, amount, category, balance_after, 0, now);
    if (!tx)
        return BB_ERR_NO_MEMORY;
    link_transaction(acc, tx);
    return BB_OK;
}

/* The balance after moving `delta` (signed) into the account, if that is allowed. */
static BbStatus balance_after(const Account *acc, Money delta, Money *out) {
    BbStatus rc = money_add(acc->balance, delta, out);
    if (rc != BB_OK)
        return rc;
    return *out < 0 ? BB_ERR_INSUFFICIENT : BB_OK;
}

/* Moves `delta` (signed) into the balance and records it, or changes nothing on failure. */
static BbStatus apply(Account *acc, TxType type, Money amount, Money delta, const char *category,
                      time_t now) {
    Money new_balance;
    BbStatus rc = balance_after(acc, delta, &new_balance);
    if (rc != BB_OK)
        return rc;
    rc = account_record_transaction(acc, type, amount, category, new_balance, now);
    if (rc != BB_OK)
        return rc;
    acc->balance = new_balance;
    return BB_OK;
}

static BbStatus check_amount(const Account *acc, Money amount) {
    if (!acc)
        return BB_ERR_INVALID_ARG;
    if (acc->closed_at)
        return BB_ERR_CLOSED;
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

BbStatus account_transfer(Account *from, Account *to, Money amount, const char *category,
                          time_t now) {
    if (!from || !to || from == to)
        return BB_ERR_INVALID_ARG;
    BbStatus rc = check_amount(from, amount);
    if (rc == BB_OK)
        rc = check_amount(to, amount);
    if (rc != BB_OK)
        return rc;
    Money from_balance, to_balance;
    if ((rc = balance_after(from, -amount, &from_balance)) != BB_OK ||
        (rc = balance_after(to, amount, &to_balance)) != BB_OK)
        return rc;

    /* Allocate both records before changing anything, so a failure leaves no half-transfer. */
    Transaction *out =
        new_transaction(TX_TRANSFER_OUT, amount, category, from_balance, to->account_number, now);
    Transaction *in =
        new_transaction(TX_TRANSFER_IN, amount, category, to_balance, from->account_number, now);
    if (!out || !in) {
        free(out);
        free(in);
        return BB_ERR_NO_MEMORY;
    }
    link_transaction(from, out);
    link_transaction(to, in);
    from->balance = from_balance;
    to->balance = to_balance;
    return BB_OK;
}

BbStatus account_close(Account *acc, time_t now) {
    if (!acc)
        return BB_ERR_INVALID_ARG;
    if (acc->closed_at)
        return BB_ERR_CLOSED;
    if (acc->balance != 0)
        return BB_ERR_NOT_EMPTY;
    acc->closed_at = now > 0 ? now : 1; /* 0 means open */
    return BB_OK;
}
