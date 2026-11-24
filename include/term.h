#ifndef TERM_H
#define TERM_H

#include <stddef.h>
#include <stdio.h>

#include "status.h"

/* Terminal presentation: optional colour, consistent messages and aligned tables. */

typedef enum Style {
    STYLE_NONE,
    STYLE_BOLD,
    STYLE_DIM,
    STYLE_TITLE,
    STYLE_GOOD, /* success, money coming in */
    STYLE_BAD,  /* errors, money going out */
    STYLE_WARN
} Style;

/*
 * Decides whether to use colour. It is used only when standard output is a terminal that
 * supports it, `allow` is set (it is cleared by --no-color), NO_COLOR is unset or empty
 * (https://no-color.org) and TERM is not "dumb". Until this is called colour is off.
 */
void term_init(int allow);
int term_colors(void);

/* The escape sequence that starts or ends `style`, or "" when colour is off. */
const char *term_start(Style style);
const char *term_end(Style style);

#ifdef __GNUC__
#define TERM_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TERM_PRINTF(fmt, args)
#endif

/* One-line messages on standard output, each followed by a newline. */
void term_success(const char *fmt, ...) TERM_PRINTF(1, 2);
void term_error(const char *fmt, ...) TERM_PRINTF(1, 2);
void term_warning(const char *fmt, ...) TERM_PRINTF(1, 2); /* prefixed with "Warning: " */
void term_heading(const char *fmt, ...) TERM_PRINTF(1, 2); /* a blank line, then the title */

/* ---------- tables ---------- */

#define TABLE_MAX_COLUMNS 8
#define TABLE_CELL_MAX    63 /* longer cell text is cut short */

typedef enum Align { ALIGN_LEFT, ALIGN_RIGHT } Align;

typedef struct Table Table;

/* A table with `columns` (1..TABLE_MAX_COLUMNS) headed by `titles`. NULL if out of memory. */
Table *table_new(size_t columns, const char *const titles[], const Align aligns[]);

/* Adds a row of `columns` cells; `styles` (one per cell) may be NULL for plain text. */
BbStatus table_add_row(Table *table, const char *const cells[], const Style styles[]);

/* Adds a separator line, e.g. above a totals row. */
BbStatus table_add_rule(Table *table);

size_t table_rows(const Table *table);

/* Prints the header, a rule and every row, with each column as wide as its widest cell. */
void table_print(const Table *table, FILE *out);

void table_free(Table *table);

#endif /* TERM_H */
