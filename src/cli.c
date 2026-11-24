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

/* Set once stdin is exhausted; every menu then unwinds and the program exits. */
static int input_closed = 0;

/* Shows `label` and reads a line. Returns 1 with a usable line, 0 on EOF or over-long input. */
static int prompt(const char *label, char *buf, size_t size) {
    printf("%s", label);
    fflush(stdout);
    switch (read_line(stdin, buf, size)) {
        case LINE_OK:
            return 1;
        case LINE_TOO_LONG:
            printf("Input too long (at most %u characters).\n", (unsigned)(size - 1));
            return 0;
        case LINE_EOF:
            break;
    }
    input_closed = 1;
    printf("\n");
    return 0;
}

/*
 * Reads a menu selection. Returns the option character ('1'..'0' + options), 0 once input is
 * closed, or -1 for anything else (after saying so).
 */
static int menu_choice(int options) {
    char buf[16];
    if (!prompt("Choice: ", buf, sizeof buf))
        return input_closed ? 0 : -1;
    if (buf[0] >= '1' && buf[0] < '1' + options && buf[1] == '\0')
        return buf[0];
    printf("Invalid option.\n");
    return -1;
}

/* The data being worked on; every change is saved straight away. */
static AccountList *session_list;
static const char *session_dir;

static int save_or_warn(void) {
    BbStatus rc = storage_save(session_dir, session_list);
    if (rc == BB_OK)
        return 1;
    printf("Warning: could not save your data (%s).\n", bb_status_message(rc));
    return 0;
}

static void create_account_flow(AccountList *list) {
    char user[USERNAME_MAX + 1];
    char pin[PIN_MAX_LEN + 1];
    if (!prompt("Enter username: ", user, sizeof user))
        return;
    if (validate_username(user) != BB_OK) {
        printf("Invalid username. Use 1-%d letters, digits, spaces or . - _ ' "
               "starting with a letter or digit.\n",
               USERNAME_MAX);
        return;
    }
    char label[48];
    snprintf(label, sizeof label, "Enter PIN (%d-%d digits): ", PIN_MIN_LEN, PIN_MAX_LEN);
    if (!prompt(label, pin, sizeof pin))
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
    save_or_warn();
    printf("Account created. Number: %lu\n", acc->account_number);
}

static Account *login_flow(AccountList *list) {
    char buf[64];
    char pin[PIN_MAX_LEN + 1];
    if (!prompt("Enter account number: ", buf, sizeof buf))
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
    if (!prompt("Enter PIN: ", pin, sizeof pin))
        return NULL;
    int ok = verify_pin(acc, pin, now);
    save_or_warn(); /* the attempt counter and lockout must survive a restart */
    if (ok) {
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
static int prompt_amount(const char *label, Money *out) {
    char buf[64];
    if (!prompt(label, buf, sizeof buf))
        return 0;
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
    if (!prompt("Category (optional): ", cat, size))
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
    char cat[CATEGORY_MAX + 1];
    if (!prompt_amount("Amount to deposit: ", &amount) || !prompt_category(cat, sizeof cat))
        return;
    BbStatus rc = account_deposit(acc, amount, cat, time(NULL));
    if (rc == BB_ERR_RANGE)
        printf("Deposit refused: the balance would exceed the account limit.\n");
    else if (rc != BB_OK)
        printf("Deposit failed: %s.\n", bb_status_message(rc));
    else if (save_or_warn())
        print_result("Deposited", amount, acc);
}

static void withdraw_flow(Account *acc) {
    Money amount;
    char cat[CATEGORY_MAX + 1];
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
    else if (save_or_warn())
        print_result("Withdrew", amount, acc);
}

static void interest_flow(Account *acc) {
    char buf[64];
    if (!prompt("Annual rate in percent (e.g. 5 or 4.75): ", buf, sizeof buf))
        return;
    int rate_bp;
    if (interest_parse_rate(buf, &rate_bp) != BB_OK) {
        printf("Invalid rate. Enter a percentage from 0 to 100 with up to two decimals.\n");
        return;
    }
    char label[64];
    snprintf(label, sizeof label,
             "Periods per year (1-%d, e.g. 12 for monthly): ", INTEREST_MAX_PERIODS);
    if (!prompt(label, buf, sizeof buf))
        return;
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
    else if (save_or_warn())
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
               "Report\n6) Logout\n");
        switch (menu_choice(6)) {
            case 0:
                return;
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
                break; /* menu_choice already explained */
        }
    }
}

void main_menu(AccountList *list, const char *data_dir) {
    session_list = list;
    session_dir = data_dir;
    for (;;) {
        printf("\n==== BankBuddy ====\n");
        printf("1) Create Account\n2) Login\n3) Exit\n");
        switch (menu_choice(3)) {
            case 0:
                return;
            case '1':
                create_account_flow(list);
                break;
            case '2': {
                Account *acc = login_flow(list);
                if (acc)
                    account_menu(acc);
            } break;
            case '3':
                printf("Goodbye.\n");
                return;
            default:
                break; /* menu_choice already explained */
        }
    }
}
