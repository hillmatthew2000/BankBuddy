#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stddef.h>
#include <time.h>

#include "money.h"
#include "status.h"

#define USERNAME_MAX 63
#define CATEGORY_MAX 31
/* Longest stored PIN credential (see auth.h for the formats). */
#define PIN_CREDENTIAL_MAX 127

/* Transaction kinds; the character is what the data file stores. */
typedef enum TxType {
    TX_DEPOSIT = 'D',
    TX_WITHDRAWAL = 'W',
    TX_INTEREST = 'I',
    TX_TRANSFER_OUT = 'S', /* sent to another account */
    TX_TRANSFER_IN = 'R'   /* received from another account */
} TxType;

typedef struct Transaction {
    unsigned long tx_id;
    TxType type;
    Money amount; /* always positive; the type says which way it moved */
    Money balance_after;
    time_t timestamp;
    unsigned long counterparty; /* the other account of a transfer, otherwise 0 */
    char category[CATEGORY_MAX + 1];
    struct Transaction *next; /* the next older transaction */
} Transaction;

typedef struct Account {
    unsigned long account_number;
    char username[USERNAME_MAX + 1];
    char pin_credential[PIN_CREDENTIAL_MAX + 1];
    Money balance;
    time_t created_at;
    time_t last_interest_applied;
    unsigned int failed_attempts;
    time_t lock_until;
    time_t closed_at; /* 0 while the account is open */
    unsigned long next_tx_id;
    Transaction *tx_head; /* newest first */
    struct Account *next;
} Account;

typedef struct AccountList {
    Account *head;
    unsigned long last_account_number; /* highest number issued so far */
} AccountList;

/* Account numbers are issued sequentially after this one. */
#define FIRST_ACCOUNT_NUMBER 100000UL

void account_list_init(AccountList *list);
void account_free_all(AccountList *list);

/* Creates an empty account with the next free number and adds it to the list. */
Account *account_create(AccountList *list, const char *username, const char *pin_credential,
                        time_t now);
Account *account_find(AccountList *list, unsigned long account_number);

/* Returns 1 if `type` is one of the TxType values. */
int tx_type_valid(int type);
/* Returns 1 if the transaction adds money to the account. */
int tx_is_credit(TxType type);

/*
 * Appends a transaction record (as the newest) without touching the balance. Used by the
 * operations below and when loading data. A NULL or empty category becomes "General".
 */
BbStatus account_record_transaction(Account *acc, TxType type, Money amount, const char *category,
                                    Money balance_after, time_t now);

/*
 * Banking operations. On failure the accounts are left unchanged. Closed accounts refuse them
 * with BB_ERR_CLOSED.
 */
BbStatus account_deposit(Account *acc, Money amount, const char *category, time_t now);
BbStatus account_withdraw(Account *acc, Money amount, const char *category, time_t now);

/*
 * Moves money between two different accounts, recording it on both. Fails with
 * BB_ERR_INSUFFICIENT if `from` cannot cover it and BB_ERR_RANGE if `to` would go over
 * MONEY_MAX.
 */
BbStatus account_transfer(Account *from, Account *to, Money amount, const char *category,
                          time_t now);

/* Closes an account with a zero balance (BB_ERR_NOT_EMPTY otherwise); its history is kept. */
BbStatus account_close(Account *acc, time_t now);

#endif /* ACCOUNT_H */
