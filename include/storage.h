#ifndef STORAGE_H
#define STORAGE_H

#include "account.h"

#define ACCOUNTS_FILE     "accounts.db"
#define TRANSACTIONS_FILE "transactions.db"

int load_data(AccountList *list);
int save_data(const AccountList *list);

#endif /* STORAGE_H */
