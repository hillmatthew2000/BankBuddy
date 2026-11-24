#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stddef.h>
#include <time.h>

#include "money.h"
#include "status.h"

#define USERNAME_MAX 63
#define CATEGORY_MAX 31

/* Transaction kinds; the character is what the data file stores. */
typedef enum TxType { TX_DEPOSIT = 'D', TX_WITHDRAWAL = 'W', TX_INTEREST = 'I' } TxType;

typedef struct Transaction {
    unsigned long tx_id;
    TxType type;
    Money amount; /* always positive; the type says which way it moved */
    Money balance_after;
    time_t timestamp;
    char category[CATEGORY_MAX + 1];
    struct Transaction *next; /* the next older transaction */
} Transaction;

typedef struct Account {
    unsigned long account_number;
    char username[USERNAME_MAX + 1];
    unsigned int pin_hash;
    Money balance;
    time_t created_at;
    time_t last_interest_applied;
    unsigned int failed_attempts;
    time_t lock_until;
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
Account *account_create(AccountList *list, const char *username, unsigned int pin_hash, time_t now);
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

/* Banking operations. On failure the account is left unchanged. */
BbStatus account_deposit(Account *acc, Money amount, const char *category, time_t now);
BbStatus account_withdraw(Account *acc, Money amount, const char *category, time_t now);

#endif /* ACCOUNT_H */
