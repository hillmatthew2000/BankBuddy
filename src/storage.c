#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "storage.h"
#include "validation.h"

int load_data(AccountList *list) {
    account_list_init(list);
    FILE *fa = fopen(ACCOUNTS_FILE, "r");
    if (fa) {
        char line[512];
        while (fgets(line, sizeof line, fa)) {
            char *p = strchr(line, '\n'); if (p) *p = '\0';
            if (line[0] == '\0') continue;
            unsigned long acc_no; char username[USERNAME_MAX+1]; unsigned int pin_hash; double balance; long created; long last_int;
            if (sscanf(line, "%lu|%63[^|]|%u|%lf|%ld|%ld", &acc_no, username, &pin_hash, &balance, &created, &last_int) == 6) {
                Account *acc = (Account*)calloc(1, sizeof(Account));
                if (!acc) break;
                acc->account_number = acc_no;
                strncpy(acc->username, username, USERNAME_MAX);
                acc->username[USERNAME_MAX] = '\0';
                acc->pin_hash = pin_hash;
                acc->balance = balance;
                acc->created_at = (time_t)created;
                acc->last_interest_applied = (time_t)last_int;
                acc->next = list->head;
                list->head = acc;
                if (acc_no > list->next_account_number) list->next_account_number = acc_no;
            }
        }
        fclose(fa);
    }

    FILE *ft = fopen(TRANSACTIONS_FILE, "r");
    if (ft) {
        char line[512];
        while (fgets(line, sizeof line, ft)) {
            char *p = strchr(line, '\n'); if (p) *p = '\0';
            if (line[0] == '\0') continue;
            unsigned long acc_no, tx_id; char type; double amount, bal_after; long ts; char category[CATEGORY_MAX+1];
            if (sscanf(line, "%lu|%lu|%c|%lf|%lf|%ld|%31[^\n]", &acc_no, &tx_id, &type, &amount, &bal_after, &ts, category) == 7) {
                Account *acc = account_find(list, acc_no);
                if (!acc) continue; /* orphan */
                Transaction *tx = (Transaction*)calloc(1, sizeof(Transaction));
                if (!tx) break;
                tx->tx_id = tx_id;
                tx->type = type;
                tx->amount = amount;
                tx->balance_after = bal_after;
                tx->timestamp = (time_t)ts;
                strncpy(tx->category, category, CATEGORY_MAX);
                tx->category[CATEGORY_MAX] = '\0';
                tx->next = acc->tx_head;
                acc->tx_head = tx;
            }
        }
        fclose(ft);
    }
    return 0;
}

int save_data(const AccountList *list) {
    FILE *fa = fopen(ACCOUNTS_FILE, "w");
    if (!fa) return -1;
    for (const Account *acc = list->head; acc; acc = acc->next) {
        fprintf(fa, "%lu|%s|%u|%.2f|%ld|%ld\n", acc->account_number, acc->username, acc->pin_hash, acc->balance, (long)acc->created_at, (long)acc->last_interest_applied);
    }
    fclose(fa);

    FILE *ft = fopen(TRANSACTIONS_FILE, "w");
    if (!ft) return -1;
    for (const Account *acc = list->head; acc; acc = acc->next) {
        for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
            fprintf(ft, "%lu|%lu|%c|%.2f|%.2f|%ld|%s\n", acc->account_number, tx->tx_id, tx->type, tx->amount, tx->balance_after, (long)tx->timestamp, tx->category);
        }
    }
    fclose(ft);
    return 0;
}
