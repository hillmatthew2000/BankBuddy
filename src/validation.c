#include "validation.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"

int read_line(char *buf, size_t max) {
    if (!fgets(buf, (int)max, stdin))
        return 0;
    char *p = strchr(buf, '\n');
    if (p)
        *p = '\0';
    return 1;
}

void trim(char *s) {
    if (!s)
        return;
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

int parse_double(const char *s, double *out) {
    if (!s || !out)
        return 0;
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

int parse_ulong(const char *s, unsigned long *out) {
    if (!s || !out)
        return 0;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

void sanitize_category(char *cat) {
    if (!cat)
        return;
    trim(cat);
    if (cat[0] == '\0') {
        strncpy(cat, "General", CATEGORY_MAX);
        cat[CATEGORY_MAX] = '\0';
    }
    if (strlen(cat) > CATEGORY_MAX)
        cat[CATEGORY_MAX] = '\0';
    for (char *p = cat; *p; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
            *p = '_';
    }
}
