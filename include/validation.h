#ifndef VALIDATION_H
#define VALIDATION_H

#include <stddef.h>
#include <stdio.h>

#include "status.h"

typedef enum LineStatus {
    LINE_OK,
    LINE_EOF,     /* input closed before anything was read */
    LINE_TOO_LONG /* the line did not fit; the rest of it was discarded */
} LineStatus;

/*
 * Reads one line into buf (at most size - 1 characters), drops the line ending (LF or CRLF)
 * and surrounding whitespace. An over-long line is consumed entirely so that its tail can't
 * leak into the next prompt.
 */
LineStatus read_line(FILE *in, char *buf, size_t size);

/* Strict unsigned decimal: digits only, no sign or spaces, no overflow. */
int parse_ulong(const char *s, unsigned long *out);

void trim(char *s);

/* Letters, digits, space and . - _ ' ; must start with a letter or digit. */
BbStatus validate_username(const char *name);

/* 1 if the two names are the same apart from upper and lower case. */
int names_match(const char *a, const char *b);

/* Trims, defaults an empty category to "General", truncates and replaces unsafe characters. */
void sanitize_category(char *cat);

#endif /* VALIDATION_H */
