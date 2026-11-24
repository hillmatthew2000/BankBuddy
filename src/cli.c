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
#include "statement.h"
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

/* Asks a yes/no question; anything but "y" or "yes" (in any case) means no. */
static int confirm(const char *question) {
    char label[160], answer[8];
    snprintf(label, sizeof label, "%s [y/N]: ", question);
    if (!prompt(label, answer, sizeof answer))
        return 0;
    for (char *p = answer; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    return strcmp(answer, "y") == 0 || strcmp(answer, "yes") == 0;
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

/* ---------- formatting ---------- */

/* "2026-01-31", or "2026-01-31 14:05" with `with_time`, in local time. */
static void format_date(time_t t, int with_time, char *buf, size_t size) {
    const struct tm *tmv = localtime(&t);
    size_t n = 0;
    if (tmv)
        n = with_time ? strftime(buf, size, "%Y-%m-%d %H:%M", tmv)
                      : strftime(buf, size, "%Y-%m-%d", tmv);
    if (n == 0)
        snprintf(buf, size, "?");
}

static void print_locked(const Account *acc, time_t now) {
    long seconds = auth_lock_remaining(acc, now);
    term_error("Too many failed attempts. Locked - try again in %ld second%s.", seconds,
               seconds == 1 ? "" : "s");
}

/* ---------- PINs, signing up and logging in ---------- */

/* Asks for a new PIN twice and stores its credential in `out`. Returns 1 on success. */
static int choose_pin(const char *label, char *out, size_t size) {
    char pin[PIN_MAX_LEN + 2], again[PIN_MAX_LEN + 2];
    char full_label[64];
    int ok = 0;
    snprintf(full_label, sizeof full_label, "%s (%d-%d digits): ", label, PIN_MIN_LEN, PIN_MAX_LEN);
    if (!prompt_secret(full_label, pin, sizeof pin)) {
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

/*
 * Asks a logged-in user for their PIN again before a sensitive change. Wrong answers count
 * towards the lockout like failed logins. Returns AUTH_LOCKED when the user must be logged out.
 */
static AuthResult reauthenticate(Account *acc) {
    char pin[PIN_MAX_LEN + 2];
    if (!prompt_secret("Current PIN: ", pin, sizeof pin))
        return AUTH_WRONG_PIN;
    time_t now = time(NULL);
    AuthResult result = verify_pin(acc, pin, now);
    secure_zero(pin, sizeof pin);
    save_or_warn();
    if (auth_lock_remaining(acc, now) > 0) {
        print_locked(acc, now);
        printf("You have been logged out.\n");
        return AUTH_LOCKED;
    }
    if (result != AUTH_OK)
        term_error("The PIN is incorrect.");
    return result;
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
    if (!choose_pin("Choose a PIN", credential, sizeof credential))
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
            if (acc->closed_at) {
                char when[16];
                format_date(acc->closed_at, 0, when, sizeof when);
                term_error("Account %lu was closed on %s.", acc->account_number, when);
                return NULL;
            }
            term_success("Welcome, %s.", acc->username);
            return acc;
        case AUTH_LOCKED: /* told apart from a wrong PIN, this would confirm the account exists */
        case AUTH_WRONG_PIN:
            break;
    }
    term_error("The account number or PIN is incorrect.");
    printf("%sAfter %d wrong PINs in a row an account is locked for %d seconds.%s\n",
           term_start(STYLE_DIM), MAX_FAILED_ATTEMPTS, LOCKOUT_SECONDS, term_end(STYLE_DIM));
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

/* Explains and returns 0 if the account cannot pay `amount`. */
static int check_funds(const Account *acc, Money amount) {
    if (amount <= acc->balance)
        return 1;
    char bal[MONEY_STR_MAX];
    money_format(acc->balance, bal, sizeof bal);
    term_error("Insufficient funds: your balance is %s.", bal);
    return 0;
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
    if (!prompt_amount("Amount: ", &amount) || !check_funds(acc, amount) ||
        !prompt_category(cat, sizeof cat))
        return;
    BbStatus rc = account_withdraw(acc, amount, cat, time(NULL));
    if (rc != BB_OK)
        term_error("The withdrawal failed: %s.", bb_status_message(rc));
    else if (save_or_warn())
        print_result("Withdrew", amount, acc);
}

static void transfer_flow(AccountList *list, Account *acc) {
    char buf[64], name[USERNAME_MAX + 1], cat[CATEGORY_MAX + 1];
    unsigned long to_no;
    Money amount;
    term_heading("Transfer");
    if (!prompt("To account number: ", buf, sizeof buf))
        return;
    if (!parse_ulong(buf, &to_no)) {
        term_error("Enter an account number such as %lu.", FIRST_ACCOUNT_NUMBER + 1);
        return;
    }
    if (to_no == acc->account_number) {
        term_error("That is this account. Choose another one.");
        return;
    }
    if (!prompt("Name on that account: ", name, sizeof name))
        return;
    /* A wrong number and a wrong name get the same answer, so that nobody can find out which
     * account numbers exist by trying them; it also catches a mistyped number. */
    Account *to = account_find(list, to_no);
    if (!to || to->closed_at || !names_match(to->username, name)) {
        term_error("There is no open account %lu in that name.", to_no);
        return;
    }
    if (!prompt_amount("Amount: ", &amount) || !check_funds(acc, amount) ||
        !prompt_category(cat, sizeof cat))
        return;

    char amt[MONEY_STR_MAX], question[160];
    money_format(amount, amt, sizeof amt);
    snprintf(question, sizeof question, "Send %s to %s, account %lu?", amt, to->username, to_no);
    if (!confirm(question)) {
        printf("Nothing was sent.\n");
        return;
    }
    BbStatus rc = account_transfer(acc, to, amount, cat, time(NULL));
    if (rc == BB_ERR_RANGE)
        term_error("Refused: account %lu cannot hold that much.", to_no);
    else if (rc != BB_OK)
        term_error("The transfer failed: %s.", bb_status_message(rc));
    else if (save_or_warn()) {
        char bal[MONEY_STR_MAX];
        money_format(acc->balance, bal, sizeof bal);
        term_success("Sent %s to account %lu. Your balance is now %s.", amt, to_no, bal);
    }
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

#define DATE_INPUT_MAX 31

typedef struct Period {
    time_t start, end;                        /* 0 = open */
    char description[2 * DATE_INPUT_MAX + 8]; /* ", <from> to <to>" */
} Period;

/* Asks for an optional date range. Returns 0 (after explaining) if it is unusable. */
static int prompt_period(Period *period) {
    char from[DATE_INPUT_MAX + 1], to[DATE_INPUT_MAX + 1];
    if (!prompt("From date (YYYY-MM-DD, Enter for the beginning): ", from, sizeof from) ||
        !prompt("To date (YYYY-MM-DD, Enter for today): ", to, sizeof to))
        return 0;
    BbStatus rc = statement_parse_range(from, to, &period->start, &period->end);
    if (rc == BB_ERR_PARSE) {
        term_error("Enter dates as YYYY-MM-DD, for example 2026-01-31.");
        return 0;
    }
    if (rc != BB_OK) {
        term_error("The period must end on or after the day it starts.");
        return 0;
    }
    if (*from && *to)
        snprintf(period->description, sizeof period->description, ", %s to %s", from, to);
    else if (*from)
        snprintf(period->description, sizeof period->description, ", from %s", from);
    else if (*to)
        snprintf(period->description, sizeof period->description, ", up to %s", to);
    else
        period->description[0] = '\0';
    return 1;
}

static void statement_flow(const Account *acc) {
    static const char *const titles[] = {"#",       "Date",   "Type",   "Category",
                                         "Account", "Amount", "Balance"};
    static const Align aligns[] = {ALIGN_RIGHT, ALIGN_LEFT,  ALIGN_LEFT, ALIGN_LEFT,
                                   ALIGN_LEFT,  ALIGN_RIGHT, ALIGN_RIGHT};
    Period period;
    term_heading("Statement");
    if (!prompt_period(&period))
        return;
    term_heading("Statement for account %lu%s", acc->account_number, period.description);
    Table *table = table_new(7, titles, aligns);
    BbStatus rc = table ? BB_OK : BB_ERR_NO_MEMORY;
    for (const Transaction *tx = acc->tx_head; tx && rc == BB_OK; tx = tx->next) {
        if (!statement_includes(tx, period.start, period.end))
            continue;
        char id[24], when[32], other[24] = "", amount[MONEY_STR_MAX + 1], balance[MONEY_STR_MAX];
        int credit = tx_is_credit(tx->type);
        snprintf(id, sizeof id, "%lu", tx->tx_id);
        format_date(tx->timestamp, 1, when, sizeof when);
        if (tx->counterparty)
            snprintf(other, sizeof other, "%lu", tx->counterparty);
        amount[0] = credit ? '+' : '-';
        money_format(tx->amount, amount + 1, sizeof amount - 1);
        money_format(tx->balance_after, balance, sizeof balance);
        const char *cells[] = {id,     when,   tx_type_name(tx->type), tx->category, other,
                               amount, balance};
        const Style styles[] = {STYLE_DIM,  STYLE_NONE, STYLE_NONE,
                                STYLE_NONE, STYLE_NONE, credit ? STYLE_GOOD : STYLE_BAD,
                                STYLE_NONE};
        rc = table_add_row(table, cells, styles);
    }
    if (rc != BB_OK)
        term_error("Could not show the statement: %s.", bb_status_message(rc));
    else if (table_rows(table) == 0)
        printf("No transactions in this period.\n");
    else
        table_print(table, stdout);
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
    Period period;
    term_heading("Spending by category");
    if (!prompt_period(&period))
        return;
    term_heading("Spending by category%s", period.description);
    BbStatus rc;
    CategorySummary *head = generate_category_report(acc, period.start, period.end, &rc);
    if (rc != BB_OK) {
        term_error("Could not build the report: %s.", bb_status_message(rc));
        return;
    }
    if (!head) {
        printf("No transactions in this period.\n");
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

static void export_flow(const Account *acc) {
    Period period;
    char suggested[64], label[112], path[512];
    term_heading("Export to CSV");
    if (!prompt_period(&period))
        return;
    snprintf(suggested, sizeof suggested, "statement-%lu.csv", acc->account_number);
    snprintf(label, sizeof label, "Save as (Enter for %s): ", suggested);
    if (!prompt(label, path, sizeof path))
        return;
    if (!*path)
        strcpy(path, suggested);

    FILE *existing = fopen(path, "rb");
    if (existing) {
        fclose(existing);
        char question[600];
        snprintf(question, sizeof question, "%s already exists. Replace it?", path);
        if (!confirm(question)) {
            printf("Nothing was exported.\n");
            return;
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        term_error("Could not create %s.", path);
        return;
    }
    unsigned long rows;
    BbStatus rc = statement_write_csv(acc, period.start, period.end, f, &rows);
    if (fclose(f) != 0 && rc == BB_OK)
        rc = BB_ERR_IO;
    if (rc != BB_OK) {
        remove(path);
        term_error("Could not write %s: %s.", path, bb_status_message(rc));
        return;
    }
    term_success("Saved %lu transaction%s to %s.", rows, rows == 1 ? "" : "s", path);
}

/* ---------- account settings ---------- */

/* Returns 0 if the user has to be logged out. */
static int change_pin_flow(Account *acc) {
    char credential[PIN_CREDENTIAL_MAX + 1];
    term_heading("Change PIN");
    AuthResult result = reauthenticate(acc);
    if (result != AUTH_OK)
        return result != AUTH_LOCKED;
    if (!choose_pin("New PIN", credential, sizeof credential))
        return 1;
    memcpy(acc->pin_credential, credential, sizeof credential);
    if (save_or_warn())
        term_success("Your PIN has been changed.");
    return 1;
}

/* Returns 0 if the user has to be logged out, which includes a successful close. */
static int close_flow(Account *acc) {
    term_heading("Close account");
    if (acc->balance != 0) {
        char bal[MONEY_STR_MAX];
        money_format(acc->balance, bal, sizeof bal);
        term_error("Your balance is %s. Withdraw or transfer it before closing the account.", bal);
        return 1;
    }
    printf("Closing is permanent: you will not be able to log in to account %lu again.\n",
           acc->account_number);
    if (!confirm("Close the account?")) {
        printf("The account is still open.\n");
        return 1;
    }
    AuthResult result = reauthenticate(acc);
    if (result != AUTH_OK)
        return result != AUTH_LOCKED;
    BbStatus rc = account_close(acc, time(NULL));
    if (rc != BB_OK) {
        term_error("The account could not be closed: %s.", bb_status_message(rc));
        return 1;
    }
    save_or_warn();
    term_success("Account %lu is closed.", acc->account_number);
    return 0;
}

/* ---------- menus ---------- */

static void account_menu(AccountList *list, Account *acc) {
    static const char *const items[] = {
        "Deposit",       "Withdraw",       "Transfer",   "Statement",     "Spending by category",
        "Export to CSV", "Apply interest", "Change PIN", "Close account", "Log out",
    };
    while (!input_closed) {
        char balance[MONEY_STR_MAX];
        money_format(acc->balance, balance, sizeof balance);
        term_heading("Account %lu - %s", acc->account_number, acc->username);
        printf("Balance: %s%s%s\n\n", term_start(STYLE_BOLD), balance, term_end(STYLE_BOLD));
        int stay = 1;
        switch (menu(items, (int)(sizeof items / sizeof items[0]))) {
            case 0:
                deposit_flow(acc);
                break;
            case 1:
                withdraw_flow(acc);
                break;
            case 2:
                transfer_flow(list, acc);
                break;
            case 3:
                statement_flow(acc);
                break;
            case 4:
                report_flow(acc);
                break;
            case 5:
                export_flow(acc);
                break;
            case 6:
                interest_flow(acc);
                break;
            case 7:
                stay = change_pin_flow(acc);
                break;
            case 8:
                stay = close_flow(acc);
                break;
            case 9:
                printf("Logged out.\n");
                return;
            default: /* input closed */
                return;
        }
        if (!stay)
            return;
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
                    account_menu(list, acc);
            } break;
            case 2:
                printf("Goodbye.\n");
                return;
            default: /* input closed */
                return;
        }
    }
}
