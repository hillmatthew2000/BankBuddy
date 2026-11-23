#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stddef.h>
#include <time.h>

#define USERNAME_MAX 63
#define CATEGORY_MAX 31

typedef struct Transaction {
    unsigned long tx_id;
    double amount;
    char type; /* 'D' deposit, 'W' withdrawal, 'I' interest */
    time_t timestamp;
    char category[CATEGORY_MAX + 1];
    double balance_after;
    struct Transaction *next;
} Transaction;

typedef struct Account {
    unsigned long account_number;
    char username[USERNAME_MAX + 1];
    unsigned int pin_hash;
    double balance;
    time_t created_at;
    time_t last_interest_applied;
    unsigned int failed_attempts;
    time_t lock_until;
    Transaction *tx_head;
    struct Account *next;
} Account;

typedef struct AccountList {
    Account *head;
    unsigned long next_account_number; /* persisted separately or recomputed */
} AccountList;

void account_list_init(AccountList *list);
Account *account_create(AccountList *list, const char *username, unsigned int pin_hash,
                        double initial_balance);
Account *account_find(AccountList *list, unsigned long account_number);
void account_free_all(AccountList *list);

/* Transaction operations */
int account_record_transaction(Account *acc, char type, double amount, const char *category,
                               double new_balance);
unsigned long account_next_tx_id(const Account *acc);

#endif /* ACCOUNT_H */
