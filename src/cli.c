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
    Account *acc = account_create(list, user, h, time(NULL));
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
    printf("%-5s %-4s %16s %16s  %-16s  %s\n", "ID", "Type", "Amount", "Balance", "Time",
           "Category");
    printf("--------------------------------------------------------------------------------\n");
    for (const Transaction *tx = acc->tx_head; tx; tx = tx->next) {
        char when[32] = "?";
        const struct tm *tmv = localtime(&tx->timestamp);
        if (tmv)
            strftime(when, sizeof when, "%Y-%m-%d %H:%M", tmv);
        char amount[MONEY_STR_MAX], balance[MONEY_STR_MAX];
        money_format(tx->amount, amount, sizeof amount);
        money_format(tx->balance_after, balance, sizeof balance);
        printf("%-5lu %-4c %16s %16s  %-16s  %s\n", tx->tx_id, (char)tx->type, amount, balance,
               when, tx->category);
    }
}

/* Prompts for an amount; prints the reason and returns 0 if it is unusable. */
static int prompt_amount(const char *prompt, Money *out) {
    char buf[64];
    printf("%s", prompt);
    if (!read_line(buf, sizeof buf))
        return 0;
    trim(buf);
    BbStatus rc = money_parse(buf, out);
    if (rc == BB_ERR_RANGE || (rc == BB_OK && *out == 0)) {
        char max[MONEY_STR_MAX];
        money_format(MONEY_MAX, max, sizeof max);
        printf("Amount must be between 0.01 and %s.\n", max);
        return 0;
    }
    if (rc != BB_OK) {
        printf("Invalid amount. Use digits with up to two decimals, e.g. 25 or 1,250.50.\n");
        return 0;
    }
    return 1;
}

static int prompt_category(char *cat, size_t size) {
    printf("Category: ");
    if (!read_line(cat, size))
        return 0;
    sanitize_category(cat);
    return 1;
}

static void print_result(const char *verb, Money amount, const Account *acc) {
    char amt[MONEY_STR_MAX], bal[MONEY_STR_MAX];
    money_format(amount, amt, sizeof amt);
    money_format(acc->balance, bal, sizeof bal);
    printf("%s %s. New balance: %s\n", verb, amt, bal);
}

static void deposit_flow(Account *acc) {
    Money amount;
    char cat[CATEGORY_MAX + 2];
    if (!prompt_amount("Amount to deposit: ", &amount) || !prompt_category(cat, sizeof cat))
        return;
    BbStatus rc = account_deposit(acc, amount, cat, time(NULL));
    if (rc == BB_ERR_RANGE)
        printf("Deposit refused: the balance would exceed the account limit.\n");
    else if (rc != BB_OK)
        printf("Deposit failed: %s.\n", bb_status_message(rc));
    else
        print_result("Deposited", amount, acc);
}

static void withdraw_flow(Account *acc) {
    Money amount;
    char cat[CATEGORY_MAX + 2];
    if (!prompt_amount("Amount to withdraw: ", &amount))
        return;
    if (amount > acc->balance) {
        printf("Insufficient funds.\n");
        return;
    }
    if (!prompt_category(cat, sizeof cat))
        return;
    BbStatus rc = account_withdraw(acc, amount, cat, time(NULL));
    if (rc != BB_OK)
        printf("Withdrawal failed: %s.\n", bb_status_message(rc));
    else
        print_result("Withdrew", amount, acc);
}

static void interest_flow(Account *acc) {
    char buf[64];
    printf("Annual rate in percent (e.g. 5 or 4.75): ");
    if (!read_line(buf, sizeof buf))
        return;
    trim(buf);
    int rate_bp;
    if (interest_parse_rate(buf, &rate_bp) != BB_OK) {
        printf("Invalid rate. Enter a percentage from 0 to 100 with up to two decimals.\n");
        return;
    }
    printf("Periods per year (1-%d, e.g. 12 for monthly): ", INTEREST_MAX_PERIODS);
    if (!read_line(buf, sizeof buf))
        return;
    trim(buf);
    unsigned long periods;
    if (!parse_ulong(buf, &periods) || periods == 0 || periods > INTEREST_MAX_PERIODS) {
        printf("Invalid number of periods.\n");
        return;
    }
    Money credited;
    BbStatus rc = apply_interest(acc, rate_bp, (unsigned)periods, time(NULL), &credited);
    if (rc != BB_OK)
        printf("Interest not applied: %s.\n", bb_status_message(rc));
    else if (credited == 0)
        printf("No interest earned for this period.\n");
    else
        print_result("Credited interest of", credited, acc);
}

static void report_flow(const Account *acc) {
    BbStatus rc;
    CategorySummary *head = generate_category_report(acc, 0, 0, &rc);
    if (rc != BB_OK) {
        printf("Could not build the report: %s.\n", bb_status_message(rc));
        return;
    }
    print_category_report(head);
    free_category_summaries(head);
}

static void account_menu(Account *acc) {
    for (;;) {
        char balance[MONEY_STR_MAX];
        money_format(acc->balance, balance, sizeof balance);
        printf("\nAccount %lu - Balance: %s\n", acc->account_number, balance);
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
