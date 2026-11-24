#include "term.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

static int colors = 0;

void term_init(int allow) {
    const char *no_color = getenv("NO_COLOR");
    const char *term = getenv("TERM");
    colors = allow && !(no_color && *no_color) && !(term && strcmp(term, "dumb") == 0) &&
             platform_stdout_ansi();
}

int term_colors(void) {
    return colors;
}

const char *term_start(Style style) {
    if (!colors)
        return "";
    switch (style) {
        case STYLE_NONE:
            return "";
        case STYLE_BOLD:
            return "\x1b[1m";
        case STYLE_DIM:
            return "\x1b[2m";
        case STYLE_TITLE:
            return "\x1b[1;36m";
        case STYLE_GOOD:
            return "\x1b[32m";
        case STYLE_BAD:
            return "\x1b[31m";
        case STYLE_WARN:
            return "\x1b[33m";
    }
    return "";
}

const char *term_end(Style style) {
    return colors && style != STYLE_NONE ? "\x1b[0m" : "";
}

static void message(Style style, const char *prefix, const char *fmt, va_list args)
    TERM_PRINTF(3, 0);

static void message(Style style, const char *prefix, const char *fmt, va_list args) {
    fputs(term_start(style), stdout);
    fputs(prefix, stdout);
    vprintf(fmt, args);
    fputs(term_end(style), stdout);
    putchar('\n');
}

void term_success(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    message(STYLE_GOOD, "", fmt, args);
    va_end(args);
}

void term_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    message(STYLE_BAD, "", fmt, args);
    va_end(args);
}

void term_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    message(STYLE_WARN, "Warning: ", fmt, args);
    va_end(args);
}

void term_heading(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    putchar('\n');
    message(STYLE_TITLE, "", fmt, args);
    va_end(args);
}

/* ---------- tables ---------- */

typedef struct Cell {
    char text[TABLE_CELL_MAX + 1];
    Style style;
    int rule; /* set on the first cell of a row that is a separator line */
} Cell;

struct Table {
    size_t columns;
    Align aligns[TABLE_MAX_COLUMNS];
    size_t widths[TABLE_MAX_COLUMNS];
    Cell titles[TABLE_MAX_COLUMNS];
    Cell *cells;
    size_t rows, capacity;
};

static size_t set_cell(Cell *cell, const char *text, Style style) {
    size_t len = text ? strlen(text) : 0;
    if (len > TABLE_CELL_MAX)
        len = TABLE_CELL_MAX;
    if (len)
        memcpy(cell->text, text, len);
    cell->text[len] = '\0';
    cell->style = style;
    cell->rule = 0;
    return len;
}

Table *table_new(size_t columns, const char *const titles[], const Align aligns[]) {
    if (columns == 0 || columns > TABLE_MAX_COLUMNS || !titles || !aligns)
        return NULL;
    Table *table = calloc(1, sizeof *table);
    if (!table)
        return NULL;
    table->columns = columns;
    for (size_t c = 0; c < columns; c++) {
        table->aligns[c] = aligns[c];
        table->widths[c] = set_cell(&table->titles[c], titles[c], STYLE_BOLD);
    }
    return table;
}

static Cell *new_row(Table *table) {
    if (table->rows == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 16;
        Cell *cells = realloc(table->cells, capacity * table->columns * sizeof *cells);
        if (!cells)
            return NULL;
        table->cells = cells;
        table->capacity = capacity;
    }
    return table->cells + table->rows++ * table->columns;
}

BbStatus table_add_row(Table *table, const char *const cells[], const Style styles[]) {
    if (!table || !cells)
        return BB_ERR_INVALID_ARG;
    Cell *row = new_row(table);
    if (!row)
        return BB_ERR_NO_MEMORY;
    for (size_t c = 0; c < table->columns; c++) {
        size_t len = set_cell(&row[c], cells[c], styles ? styles[c] : STYLE_NONE);
        if (len > table->widths[c])
            table->widths[c] = len;
    }
    return BB_OK;
}

BbStatus table_add_rule(Table *table) {
    if (!table)
        return BB_ERR_INVALID_ARG;
    Cell *row = new_row(table);
    if (!row)
        return BB_ERR_NO_MEMORY;
    for (size_t c = 0; c < table->columns; c++)
        set_cell(&row[c], "", STYLE_NONE);
    row[0].rule = 1;
    return BB_OK;
}

size_t table_rows(const Table *table) {
    return table ? table->rows : 0;
}

static void print_spaces(FILE *out, size_t n) {
    while (n--)
        fputc(' ', out);
}

static void print_rule(const Table *table, FILE *out) {
    size_t width = 0;
    for (size_t c = 0; c < table->columns; c++)
        width += table->widths[c] + (c ? 2 : 0);
    fputs(term_start(STYLE_DIM), out);
    for (size_t i = 0; i < width; i++)
        fputc('-', out);
    fputs(term_end(STYLE_DIM), out);
    fputc('\n', out);
}

static void print_row(const Table *table, const Cell *row, FILE *out) {
    for (size_t c = 0; c < table->columns; c++) {
        const Cell *cell = &row[c];
        size_t pad = table->widths[c] - strlen(cell->text);
        int last = c + 1 == table->columns;
        if (c)
            fputs("  ", out);
        if (table->aligns[c] == ALIGN_RIGHT)
            print_spaces(out, pad);
        fprintf(out, "%s%s%s", term_start(cell->style), cell->text, term_end(cell->style));
        if (table->aligns[c] == ALIGN_LEFT && !last)
            print_spaces(out, pad); /* no trailing spaces at the end of a line */
    }
    fputc('\n', out);
}

void table_print(const Table *table, FILE *out) {
    if (!table || !out)
        return;
    print_row(table, table->titles, out);
    print_rule(table, out);
    for (size_t r = 0; r < table->rows; r++) {
        const Cell *row = table->cells + r * table->columns;
        if (row[0].rule)
            print_rule(table, out);
        else
            print_row(table, row, out);
    }
}

void table_free(Table *table) {
    if (!table)
        return;
    free(table->cells);
    free(table);
}
