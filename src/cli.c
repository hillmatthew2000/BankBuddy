#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "account.h"
#include "auth.h"
#include "interest.h"
#include "report.h"
#include "storage.h"
#include "validation.h"

static void create_account_flow(AccountList *list) {
    char user[USERNAME_MAX + 2];
    char pin[PIN_MAX_LEN + 2];
    printf("Enter username: ");
    if (!read_line(user, sizeof user))
        return;
    if (user[0] == '\0')
        return;
    printf("Enter PIN (%d-%d digits): ", PIN_MIN_LEN, PIN_MAX_LEN);
    if (!read_line(pin, sizeof pin))
        return;
    if (!validate_pin_format(pin)) {
        printf("Invalid PIN format.\n");
        return;
    }
    unsigned int h = hash_pin(pin);
    Account *acc = account_create(list, user, h, 0.0);
    if (!acc) {
        printf("Failed to create account.\n");
        return;
    }
    printf("Account created. Number: %lu\n", acc->account_number);
}

static Account *login_flow(AccountList *list) {
    char buf[64];
    char pin[PIN_MAX_LEN + 2];
    printf("Enter account number: ");
    if (!read_line(buf, sizeof buf))
        return NULL;
    unsigned long acc_no;
    if (!parse_ulong(buf, &acc_no)) {
        printf("Invalid number.\n");
        return NULL;
    }
    Account *acc = account_find(list, acc_no);
    if (!acc) {
        printf("No such account.\n");
        return NULL;
    }
    time_t now = time(NULL);
    if (acc->lock_until && now < acc->lock_until) {
        printf("Account locked. Try later.\n");
        return NULL;
    }
    printf("Enter PIN: ");
    if (!read_line(pin, sizeof pin))
        return NULL;
    if (verify_pin(acc, pin, now)) {
        printf("Login successful. Welcome %s!\n", acc->username);
        return acc;
    }
    printf("Login failed.\n");
    return NULL;
}

static void show_transactions(const Account *acc) {
    printf("ID   Type  Amount      BalanceAfter   Time                Category\n");
    printf("-----------------------------------------------------------------------\n");
    for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
        char buf[32];
        struct tm *tmv = localtime(&tx->timestamp);
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tmv);
        printf("%-4lu %-5c %10.2f %14.2f %s %-s\n", tx->tx_id, tx->type, tx->amount,
               tx->balance_after, buf, tx->category);
    }
}

static void deposit_flow(Account *acc) {
    char buf[64];
    char cat[CATEGORY_MAX + 2];
    printf("Amount to deposit: ");
    if (!read_line(buf, sizeof buf))
        return;
    double amt;
    if (!parse_double(buf, &amt) || amt <= 0) {
        printf("Invalid amount.\n");
        return;
    }
    printf("Category: ");
    if (!read_line(cat, sizeof cat))
        return;
    sanitize_category(cat);
    acc->balance += amt;
    account_record_transaction(acc, 'D', amt, cat, acc->balance);
    printf("Deposited %.2f. New balance: %.2f\n", amt, acc->balance);
}

static void withdraw_flow(Account *acc) {
    char buf[64];
    char cat[CATEGORY_MAX + 2];
    printf("Amount to withdraw: ");
    if (!read_line(buf, sizeof buf))
        return;
    double amt;
    if (!parse_double(buf, &amt) || amt <= 0) {
        printf("Invalid amount.\n");
        return;
    }
    if (amt > acc->balance) {
        printf("Insufficient funds.\n");
        return;
    }
    printf("Category: ");
    if (!read_line(cat, sizeof cat))
        return;
    sanitize_category(cat);
    acc->balance -= amt;
    account_record_transaction(acc, 'W', amt, cat, acc->balance);
    printf("Withdrew %.2f. New balance: %.2f\n", amt, acc->balance);
}

static void interest_flow(Account *acc) {
    char buf[64];
    printf("Annual rate (e.g., 0.05 for 5%%): ");
    if (!read_line(buf, sizeof buf))
        return;
    double rate;
    if (!parse_double(buf, &rate) || rate < 0) {
        printf("Invalid rate.\n");
        return;
    }
    printf("Periods per year (e.g., 12): ");
    if (!read_line(buf, sizeof buf))
        return;
    double periods_d;
    if (!parse_double(buf, &periods_d) || periods_d <= 0) {
        printf("Invalid periods.\n");
        return;
    }
    unsigned int periods = (unsigned int)periods_d;
    apply_interest(acc, rate, periods, time(NULL));
    printf("Interest applied. New balance: %.2f\n", acc->balance);
}

static void report_flow(const Account *acc) {
    CategorySummary *head = generate_category_report(acc, 0, 0);
    print_category_report(head);
    free_category_summaries(head);
}

static void account_menu(Account *acc) {
    for (;;) {
        printf("\nAccount %lu - Balance: %.2f\n", acc->account_number, acc->balance);
        printf("1) Deposit\n2) Withdraw\n3) Transactions\n4) Apply Interest\n5) Category "
               "Report\n6) Logout\nChoice: ");
        char choice[8];
        if (!read_line(choice, sizeof choice))
            return;
        switch (choice[0]) {
            case '1':
                deposit_flow(acc);
                break;
            case '2':
                withdraw_flow(acc);
                break;
            case '3':
                show_transactions(acc);
                break;
            case '4':
                interest_flow(acc);
                break;
            case '5':
                report_flow(acc);
                break;
            case '6':
                return;
            default:
                printf("Invalid option.\n");
        }
    }
}

void main_menu(AccountList *list) {
    for (;;) {
        printf("\n==== BankBuddy ====\n");
        printf("1) Create Account\n2) Login\n3) Save\n4) Exit\nChoice: ");
        char choice[8];
        if (!read_line(choice, sizeof choice))
            break;
        switch (choice[0]) {
            case '1':
                create_account_flow(list);
                break;
            case '2': {
                Account *acc = login_flow(list);
                if (acc)
                    account_menu(acc);
            } break;
            case '3':
                save_data(list);
                printf("Saved.\n");
                break;
            case '4':
                save_data(list);
                printf("Goodbye.\n");
                return;
            default:
                printf("Invalid option.\n");
        }
    }
}
