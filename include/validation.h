#ifndef VALIDATION_H
#define VALIDATION_H

#include <stddef.h>

int read_line(char *buf, size_t max);
int parse_ulong(const char *s, unsigned long *out);
void trim(char *s);
void sanitize_category(char *cat);

#endif /* VALIDATION_H */
