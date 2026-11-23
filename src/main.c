#include <stdio.h>
#include <time.h>

#include "account.h"
#include "auth.h"
#include "cli.h"
#include "storage.h"

static void seed_demo(AccountList *list) {
    if (list->head)
        return; /* already have data */
    unsigned int pin_hash = hash_pin("1234");
    Account *demo = account_create(list, "demo", pin_hash, 500.0);
    if (demo) {
        account_record_transaction(demo, 'D', 500.0, "Seed", demo->balance);
    }
}

int main(void) {
    AccountList list;
    load_data(&list);
    seed_demo(&list);
    main_menu(&list);
    account_free_all(&list);
    return 0;
}
