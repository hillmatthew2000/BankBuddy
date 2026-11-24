#include <stdio.h>
#include <string.h>
#include <time.h>

#include "account.h"
#include "auth.h"
#include "cli.h"
#include "storage.h"
#include "term.h"

#define DEMO_PIN     "1234"
#define DEMO_BALANCE 50000 /* 500.00 */

static void usage(FILE *out) {
    fprintf(out,
            "Usage: bankbuddy [--data-dir DIR] [--demo] [--no-color]\n"
            "\n"
            "  -d, --data-dir DIR   keep data in DIR (default: current directory)\n"
            "      --demo           add a demo account (PIN %s) if there are no accounts yet\n"
            "      --no-color       plain output (also when NO_COLOR is set)\n"
            "  -h, --help           show this help\n",
            DEMO_PIN);
}

/* Adds an account with some money in it, for trying the program out. */
static Account *seed_demo(AccountList *list) {
    time_t now = time(NULL);
    char credential[PIN_CREDENTIAL_MAX + 1];
    if (pin_credential_create(DEMO_PIN, PIN_HASH_ITERATIONS, credential, sizeof credential) !=
        BB_OK)
        return NULL;
    Account *demo = account_create(list, "demo", credential, now);
    if (demo)
        account_deposit(demo, DEMO_BALANCE, "Seed", now);
    return demo;
}

int main(int argc, char **argv) {
    const char *data_dir = ".";
    int allow_color = 1;
    int want_demo = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data-dir") == 0) && i + 1 < argc) {
            data_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--demo") == 0) {
            want_demo = 1;
            continue;
        }
        if (strcmp(argv[i], "--no-color") == 0) {
            allow_color = 0;
            continue;
        }
        fprintf(stderr, "bankbuddy: unknown or incomplete option '%s'\n\n", argv[i]);
        usage(stderr);
        return 2;
    }

    term_init(allow_color);

    AccountList list;
    LoadReport report;
    BbStatus rc = storage_load(data_dir, &list, &report);
    if (rc != BB_OK) {
        fprintf(stderr, "bankbuddy: cannot load %s", report.path);
        if (rc == BB_ERR_CORRUPT && report.error_line)
            fprintf(stderr, " (line %lu)", report.error_line);
        fprintf(stderr, ": %s.\nNothing was changed. Repair or move the file and try again.\n",
                bb_status_message(rc));
        return 1;
    }

    int needs_save = 0;
    if (report.migrated_legacy) {
        printf("Converted data from %s and %s to %s. The old files were left in place.\n",
               LEGACY_ACCOUNTS, LEGACY_TRANSACTIONS, DATA_FILE);
        if (report.skipped_legacy)
            term_warning("%lu unreadable line(s) in the old files were skipped.",
                         report.skipped_legacy);
        needs_save = 1;
    }
    if (want_demo && list.head) {
        term_warning("--demo only adds the demo account when there are no accounts yet.");
    } else if (want_demo) {
        Account *demo = seed_demo(&list);
        if (demo) {
            printf("Added a demo account: number %lu, PIN %s.\n", demo->account_number, DEMO_PIN);
            needs_save = 1;
        } else {
            term_warning("the demo account could not be created.");
        }
    } else if (!list.head) {
        printf("No accounts yet: choose 1 to create one, or restart with --demo to try a sample "
               "account.\n");
    }
    if (needs_save && (rc = storage_save(data_dir, &list)) != BB_OK)
        term_warning("could not save to %s: %s.", data_dir, bb_status_message(rc));

    main_menu(&list, data_dir);
    account_free_all(&list);
    return 0;
}
