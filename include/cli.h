#ifndef CLI_H
#define CLI_H

#include "account.h"

/* Runs the interactive menus until the user exits or input ends. Changes are saved to
 * data_dir
 * as they happen. */
void main_menu(AccountList *list, const char *data_dir);

#endif /* CLI_H */
