#include <stdio.h>
#include <string.h>

#include "check.h"
#include "term.h"

/* Prints `table` to a scratch file and reads the text back into `buf`. */
static void render(const Table *table, char *buf, size_t size) {
    char path[512];
    test_temp_path(path, sizeof path, "table.txt");
    buf[0] = '\0';
    FILE *f = fopen(path, "w+b");
    if (!f)
        return;
    table_print(table, f);
    rewind(f);
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    remove(path);
}

static void test_alignment(void) {
    const char *titles[] = {"Name", "Amount"};
    const Align aligns[] = {ALIGN_LEFT, ALIGN_RIGHT};
    Table *table = table_new(2, titles, aligns);
    CHECK(table != NULL);
    if (!table)
        return;
    const char *row1[] = {"Rent", "1,200.00"};
    const char *row2[] = {"Coffee shop", "3.50"};
    const char *total[] = {"Total", "1,203.50"};
    CHECK_INT_EQ(table_add_row(table, row1, NULL), BB_OK);
    CHECK_INT_EQ(table_add_row(table, row2, NULL), BB_OK);
    CHECK_INT_EQ(table_add_rule(table), BB_OK);
    CHECK_INT_EQ(table_add_row(table, total, NULL), BB_OK);
    CHECK_INT_EQ(table_rows(table), 4);

    char out[512];
    render(table, out, sizeof out);
    /* Columns as wide as their widest cell, two spaces apart, no trailing spaces. */
    CHECK_STR_EQ(out, "Name           Amount\n"
                      "---------------------\n"
                      "Rent         1,200.00\n"
                      "Coffee shop      3.50\n"
                      "---------------------\n"
                      "Total        1,203.50\n");
    table_free(table);
}

static void test_limits(void) {
    const char *titles[] = {"A"};
    const Align aligns[] = {ALIGN_LEFT};
    CHECK(table_new(0, titles, aligns) == NULL);
    CHECK(table_new(TABLE_MAX_COLUMNS + 1, titles, aligns) == NULL);

    Table *table = table_new(1, titles, aligns);
    CHECK(table != NULL);
    if (!table)
        return;
    /* Long cells are cut short; many rows make the table grow. */
    char long_text[TABLE_CELL_MAX + 20];
    memset(long_text, 'x', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    const char *cells[] = {long_text};
    for (int i = 0; i < 100; i++)
        CHECK_INT_EQ(table_add_row(table, cells, NULL), BB_OK);
    CHECK_INT_EQ(table_rows(table), 100);

    char out[16384];
    render(table, out, sizeof out);
    const char *first_row = strchr(strchr(out, '\n') + 1, '\n') + 1;
    CHECK_INT_EQ(strcspn(first_row, "\n"), TABLE_CELL_MAX);
    table_free(table);
    table_free(NULL);
}

/* Without term_init() colour is off, so nothing writes escape sequences. */
static void test_plain_by_default(void) {
    CHECK(!term_colors());
    CHECK_STR_EQ(term_start(STYLE_BAD), "");
    CHECK_STR_EQ(term_end(STYLE_BAD), "");
}

void suite_term(void) {
    test_alignment();
    test_limits();
    test_plain_by_default();
}
