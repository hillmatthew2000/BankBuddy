#include "validation.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"

LineStatus read_line(FILE *in, char *buf, size_t size) {
    if (!in || !buf || size < 2 || size > (size_t)INT_MAX)
        return LINE_EOF;
    if (!fgets(buf, (int)size, in))
        return LINE_EOF;

    LineStatus status = LINE_OK;
    char *newline = strchr(buf, '\n');
    if (newline) {
        *newline = '\0';
    } else {
        /* No newline: either the input ended, or the line is longer than the buffer. */
        int c = getc(in);
        if (c != EOF && c != '\n') {
            status = LINE_TOO_LONG;
            while (c != EOF && c != '\n')
                c = getc(in);
        }
    }
    trim(buf);
    return status;
}

void trim(char *s) {
    if (!s)
        return;
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    size_t len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1]))
        len--;
    memmove(s, start, len);
    s[len] = '\0';
}

int parse_ulong(const char *s, unsigned long *out) {
    if (!s || !out || !isdigit((unsigned char)*s))
        return 0;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

BbStatus validate_username(const char *name) {
    if (!name)
        return BB_ERR_INVALID_ARG;
    size_t len = strlen(name);
    if (len == 0 || len > USERNAME_MAX)
        return BB_ERR_RANGE;
    if (!isalnum((unsigned char)name[0]))
        return BB_ERR_PARSE;
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!isalnum(c) && c != ' ' && c != '.' && c != '-' && c != '_' && c != '\'')
            return BB_ERR_PARSE;
    }
    return BB_OK;
}

int names_match(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

void sanitize_category(char *cat) {
    if (!cat)
        return;
    trim(cat);
    if (strlen(cat) > CATEGORY_MAX) {
        cat[CATEGORY_MAX] = '\0';
        trim(cat);
    }
    if (cat[0] == '\0') {
        strcpy(cat, "General");
        return;
    }
    for (char *p = cat; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!isalnum(c) && c != ' ' && c != '_' && c != '-' && c != '.' && c != '&' && c != '/')
            *p = '_';
    }
}
