#include <stdio.h>
#include <time.h>

#include "account.h"
#include "auth.h"
#include "cli.h"
#include "storage.h"

static void seed_demo(AccountList *list) {
    if (list->head)
        return; /* already have data */
    time_t now = time(NULL);
    Account *demo = account_create(list, "demo", hash_pin("1234"), now);
    if (demo)
        account_deposit(demo, 50000, "Seed", now);
}

int main(void) {
    AccountList list;
    load_data(&list);
    seed_demo(&list);
    main_menu(&list);
    account_free_all(&list);
    return 0;
}
