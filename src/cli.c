#include "cli.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "account.h"
#include "auth.h"
#include "interest.h"
#include "platform.h"
#include "report.h"
#include "sha256.h"
#include "storage.h"
#include "term.h"
#include "validation.h"

/* ---------- input ---------- */

/* Set once stdin is exhausted; every menu then unwinds and the program exits. */
static int input_closed = 0;

/*
 * Shows `label` and reads a line, without echoing it on a terminal if `secret`. Returns 1 with
 * a usable line, 0 on EOF or over-long input.
 */
static int ask(const char *label, char *buf, size_t size, int secret) {
    printf("%s%s%s", term_start(STYLE_BOLD), label, term_end(STYLE_BOLD));
    fflush(stdout);
    int hidden = secret && platform_stdin_echo(0);
    LineStatus status = read_line(stdin, buf, size);
    if (hidden) {
        platform_stdin_echo(1);
        printf("\n"); /* the Enter key was not echoed either */
    }
    switch (status) {
        case LINE_OK:
            return 1;
        case LINE_TOO_LONG:
            term_error("Too long: at most %u characters.", (unsigned)(size - 1));
            return 0;
        case LINE_EOF:
            break;
    }
    input_closed = 1;
    printf("\n");
    return 0;
}

static int prompt(const char *label, char *buf, size_t size) {
    return ask(label, buf, size, 0);
}

static int prompt_secret(const char *label, char *buf, size_t size) {
    return ask(label, buf, size, 1);
}

#define MENU_CLOSED (-1)

/*
 * Shows numbered `items` and reads a choice. The last item (the way back out) is numbered 0,
 * the others from 1. Returns the index into `items`, or MENU_CLOSED once input has ended.
 * Invalid choices are explained and asked again.
 */
static int menu(const char *const items[], int count) {
    for (int i = 0; i < count; i++) {
        int key = i + 1 < count ? i + 1 : 0;
        printf("  %s%d%s  %s\n", term_start(STYLE_BOLD), key, term_end(STYLE_BOLD), items[i]);
    }
    for (;;) {
        char buf[16];
        unsigned long choice;
        if (!prompt("Choose an option: ", buf, sizeof buf)) {
            if (input_closed)
                return MENU_CLOSED;
            continue;
        }
        if (parse_ulong(buf, &choice) && choice < (unsigned long)count)
            return choice == 0 ? count - 1 : (int)choice - 1;
        term_error("Please enter a number from 0 to %d.", count - 1);
    }
}

/* ---------- saving ---------- */

/* The data being worked on; every change is saved straight away. */
static AccountList *session_list;
static const char *session_dir;

static int save_or_warn(void) {
    BbStatus rc = storage_save(session_dir, session_list);
    if (rc == BB_OK)
        return 1;
    term_warning("your changes could not be saved (%s).", bb_status_message(rc));
    return 0;
}

/* ---------- signing up and logging in ---------- */

/* Asks for a new PIN twice and stores its credential in `out`. Returns 1 on success. */
static int choose_pin(char *out, size_t size) {
    char pin[PIN_MAX_LEN + 2], again[PIN_MAX_LEN + 2];
    char label[48];
    int ok = 0;
    snprintf(label, sizeof label, "Choose a PIN (%d-%d digits): ", PIN_MIN_LEN, PIN_MAX_LEN);
    if (!prompt_secret(label, pin, sizeof pin)) {
        /* nothing usable was entered */
    } else if (!validate_pin_format(pin)) {
        term_error("A PIN must be %d to %d digits.", PIN_MIN_LEN, PIN_MAX_LEN);
    } else if (!prompt_secret("Repeat the PIN: ", again, sizeof again)) {
        /* nothing usable was entered */
    } else if (strcmp(pin, again) != 0) {
        term_error("The PINs do not match.");
    } else {
        BbStatus rc = pin_credential_create(pin, PIN_HASH_ITERATIONS, out, size);
        if (rc == BB_OK)
            ok = 1;
        else
            term_error("Could not secure the PIN: %s.", bb_status_message(rc));
    }
    secure_zero(pin, sizeof pin);
    secure_zero(again, sizeof again);
    return ok;
}

static void create_account_flow(AccountList *list) {
    char user[USERNAME_MAX + 1];
    char credential[PIN_CREDENTIAL_MAX + 1];
    term_heading("Create an account");
    if (!prompt("Your name: ", user, sizeof user))
        return;
    if (validate_username(user) != BB_OK) {
        term_error("Use 1-%d letters, digits, spaces or . - _ ' starting with a letter or digit.",
                   USERNAME_MAX);
        return;
    }
    if (!choose_pin(credential, sizeof credential))
        return;
    Account *acc = account_create(list, user, credential, time(NULL));
    if (!acc) {
        term_error("The account could not be created: %s.", bb_status_message(BB_ERR_NO_MEMORY));
        return;
    }
    save_or_warn();
    term_success("Account %lu created. Use this number and your PIN to log in.",
                 acc->account_number);
}

static Account *login_flow(AccountList *list) {
    char buf[64];
    char pin[PIN_MAX_LEN + 2];
    term_heading("Log in");
    if (!prompt("Account number: ", buf, sizeof buf) || !prompt_secret("PIN: ", pin, sizeof pin))
        return NULL;
    /* The same answer for an unknown account and a wrong PIN, after the same amount of work,
     * so that nobody can find out which account numbers exist. */
    unsigned long acc_no;
    Account *acc = parse_ulong(buf, &acc_no) ? account_find(list, acc_no) : NULL;
    AuthResult result = AUTH_WRONG_PIN;
    time_t now = time(NULL);
    if (acc) {
        result = verify_pin(acc, pin, now);
        save_or_warn(); /* the attempt counter, lockout and any re-hash must survive a restart */
    } else {
        auth_dummy_check(pin);
    }
    secure_zero(pin, sizeof pin);

    switch (result) {
        case AUTH_OK:
            term_success("Welcome, %s.", acc->username);
            return acc;
        case AUTH_LOCKED: {
            long seconds = auth_lock_remaining(acc, now);
            term_error("Too many failed attempts. Locked - try again in %ld second%s.", seconds,
                       seconds == 1 ? "" : "s");
            return NULL;
        }
        case AUTH_WRONG_PIN:
            break;
    }
    term_error("The account number or PIN is incorrect.");
    return NULL;
}

/* ---------- money in and out ---------- */

/* Prompts for an amount; explains and returns 0 if it is unusable. */
static int prompt_amount(const char *label, Money *out) {
    char buf[64];
    if (!prompt(label, buf, sizeof buf))
        return 0;
    BbStatus rc = money_parse(buf, out);
    if (rc == BB_ERR_RANGE || (rc == BB_OK && *out == 0)) {
        char max[MONEY_STR_MAX];
        money_format(MONEY_MAX, max, sizeof max);
        term_error("The amount must be between 0.01 and %s.", max);
        return 0;
    }
    if (rc != BB_OK) {
        term_error("Enter an amount like 25, 25.50 or 1,250.50.");
        return 0;
    }
    return 1;
}

static int prompt_category(char *cat, size_t size) {
    if (!prompt("Category (Enter for General): ", cat, size))
        return 0;
    sanitize_category(cat);
    return 1;
}

static void print_result(const char *what, Money amount, const Account *acc) {
    char amt[MONEY_STR_MAX], bal[MONEY_STR_MAX];
    money_format(amount, amt, sizeof amt);
    money_format(acc->balance, bal, sizeof bal);
    term_success("%s %s. Your balance is now %s.", what, amt, bal);
}

static void deposit_flow(Account *acc) {
    Money amount;
    char cat[CATEGORY_MAX + 1];
    term_heading("Deposit");
    if (!prompt_amount("Amount: ", &amount) || !prompt_category(cat, sizeof cat))
        return;
    BbStatus rc = account_deposit(acc, amount, cat, time(NULL));
    if (rc == BB_ERR_RANGE)
        term_error("Refused: the balance would go over the account limit.");
    else if (rc != BB_OK)
        term_error("The deposit failed: %s.", bb_status_message(rc));
    else if (save_or_warn())
        print_result("Deposited", amount, acc);
}

static void withdraw_flow(Account *acc) {
    Money amount;
    char cat[CATEGORY_MAX + 1];
    term_heading("Withdraw");
    if (!prompt_amount("Amount: ", &amount))
        return;
    if (amount > acc->balance) {
        char bal[MONEY_STR_MAX];
        money_format(acc->balance, bal, sizeof bal);
        term_error("Insufficient funds: your balance is %s.", bal);
        return;
    }
    if (!prompt_category(cat, sizeof cat))
        return;
    BbStatus rc = account_withdraw(acc, amount, cat, time(NULL));
    if (rc != BB_OK)
        term_error("The withdrawal failed: %s.", bb_status_message(rc));
    else if (save_or_warn())
        print_result("Withdrew", amount, acc);
}

static void interest_flow(Account *acc) {
    char buf[64];
    term_heading("Apply interest");
    if (!prompt("Yearly rate in percent (e.g. 5 or 4.75): ", buf, sizeof buf))
        return;
    int rate_bp;
    if (interest_parse_rate(buf, &rate_bp) != BB_OK) {
        term_error("Enter a percentage from 0 to 100 with up to two decimals.");
        return;
    }
    char label[64];
    snprintf(label, sizeof label,
             "Periods per year (1-%d, e.g. 12 for monthly): ", INTEREST_MAX_PERIODS);
    if (!prompt(label, buf, sizeof buf))
        return;
    unsigned long periods;
    if (!parse_ulong(buf, &periods) || periods == 0 || periods > INTEREST_MAX_PERIODS) {
        term_error("Enter a whole number from 1 to %d.", INTEREST_MAX_PERIODS);
        return;
    }
    Money credited;
    BbStatus rc = apply_interest(acc, rate_bp, (unsigned)periods, time(NULL), &credited);
    if (rc != BB_OK)
        term_error("Interest was not applied: %s.", bb_status_message(rc));
    else if (credited == 0)
        printf("No interest was earned for this period.\n");
    else if (save_or_warn())
        print_result("Credited interest of", credited, acc);
}

/* ---------- statements and reports ---------- */

static const char *tx_type_name(TxType type) {
    switch (type) {
        case TX_DEPOSIT:
            return "Deposit";
        case TX_WITHDRAWAL:
            return "Withdrawal";
        case TX_INTEREST:
            return "Interest";
    }
    return "?";
}

static void format_time(time_t t, char *buf, size_t size) {
    const struct tm *tmv = localtime(&t);
    if (!tmv || strftime(buf, size, "%Y-%m-%d %H:%M", tmv) == 0)
        snprintf(buf, size, "?");
}

static void statement_flow(const Account *acc) {
    static const char *const titles[] = {"#", "Date", "Type", "Category", "Amount", "Balance"};
    static const Align aligns[] = {ALIGN_RIGHT, ALIGN_LEFT,  ALIGN_LEFT,
                                   ALIGN_LEFT,  ALIGN_RIGHT, ALIGN_RIGHT};
    term_heading("Statement for account %lu", acc->account_number);
    if (!acc->tx_head) {
        printf("No transactions yet.\n");
        return;
    }
    Table *table = table_new(6, titles, aligns);
    BbStatus rc = table ? BB_OK : BB_ERR_NO_MEMORY;
    for (const Transaction *tx = acc->tx_head; tx && rc == BB_OK; tx = tx->next) {
        char id[24], when[32], amount[MONEY_STR_MAX + 1], balance[MONEY_STR_MAX];
        int credit = tx_is_credit(tx->type);
        snprintf(id, sizeof id, "%lu", tx->tx_id);
        format_time(tx->timestamp, when, sizeof when);
        amount[0] = credit ? '+' : '-';
        money_format(tx->amount, amount + 1, sizeof amount - 1);
        money_format(tx->balance_after, balance, sizeof balance);
        const char *cells[] = {id, when, tx_type_name(tx->type), tx->category, amount, balance};
        const Style styles[] = {
            STYLE_DIM, STYLE_NONE, STYLE_NONE, STYLE_NONE, credit ? STYLE_GOOD : STYLE_BAD,
            STYLE_NONE};
        rc = table_add_row(table, cells, styles);
    }
    if (rc == BB_OK)
        table_print(table, stdout);
    else
        term_error("Could not show the statement: %s.", bb_status_message(rc));
    table_free(table);
}

static BbStatus add_report_row(Table *table, const char *name, Money in, Money out, Style style) {
    char credits[MONEY_STR_MAX], debits[MONEY_STR_MAX], net[MONEY_STR_MAX];
    money_format(in, credits, sizeof credits);
    money_format(out, debits, sizeof debits);
    money_format(in - out, net, sizeof net);
    const char *cells[] = {name, credits, debits, net};
    const Style styles[] = {style, style, style, in - out < 0 ? STYLE_BAD : style};
    return table_add_row(table, cells, styles);
}

static void report_flow(const Account *acc) {
    static const char *const titles[] = {"Category", "Money in", "Money out", "Net"};
    static const Align aligns[] = {ALIGN_LEFT, ALIGN_RIGHT, ALIGN_RIGHT, ALIGN_RIGHT};
    term_heading("Spending by category");
    BbStatus rc;
    CategorySummary *head = generate_category_report(acc, 0, 0, &rc);
    if (rc != BB_OK) {
        term_error("Could not build the report: %s.", bb_status_message(rc));
        return;
    }
    if (!head) {
        printf("No transactions yet.\n");
        return;
    }
    Table *table = table_new(4, titles, aligns);
    rc = table ? BB_OK : BB_ERR_NO_MEMORY;
    Money total_in = 0, total_out = 0;
    for (const CategorySummary *c = head; c && rc == BB_OK; c = c->next) {
        rc = add_report_row(table, c->category, c->credits, c->debits, STYLE_NONE);
        total_in += c->credits;
        total_out += c->debits;
    }
    if (rc == BB_OK && head->next) {
        rc = table_add_rule(table);
        if (rc == BB_OK)
            rc = add_report_row(table, "Total", total_in, total_out, STYLE_BOLD);
    }
    if (rc == BB_OK)
        table_print(table, stdout);
    else
        term_error("Could not show the report: %s.", bb_status_message(rc));
    table_free(table);
    free_category_summaries(head);
}

/* ---------- menus ---------- */

static void account_menu(Account *acc) {
    static const char *const items[] = {
        "Deposit", "Withdraw", "Statement", "Spending by category", "Apply interest", "Log out",
    };
    while (!input_closed) {
        char balance[MONEY_STR_MAX];
        money_format(acc->balance, balance, sizeof balance);
        term_heading("Account %lu - %s", acc->account_number, acc->username);
        printf("Balance: %s%s%s\n\n", term_start(STYLE_BOLD), balance, term_end(STYLE_BOLD));
        switch (menu(items, (int)(sizeof items / sizeof items[0]))) {
            case 0:
                deposit_flow(acc);
                break;
            case 1:
                withdraw_flow(acc);
                break;
            case 2:
                statement_flow(acc);
                break;
            case 3:
                report_flow(acc);
                break;
            case 4:
                interest_flow(acc);
                break;
            case 5:
                printf("Logged out.\n");
                return;
            default: /* input closed */
                return;
        }
    }
}

void main_menu(AccountList *list, const char *data_dir) {
    static const char *const items[] = {"Create an account", "Log in", "Exit"};
    session_list = list;
    session_dir = data_dir;
    while (!input_closed) {
        term_heading("BankBuddy");
        switch (menu(items, (int)(sizeof items / sizeof items[0]))) {
            case 0:
                create_account_flow(list);
                break;
            case 1: {
                Account *acc = login_flow(list);
                if (acc)
                    account_menu(acc);
            } break;
            case 2:
                printf("Goodbye.\n");
                return;
            default: /* input closed */
                return;
        }
    }
}
