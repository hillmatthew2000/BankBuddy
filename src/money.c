#include "money.h"

#include <string.h>

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Accumulates one digit into *value, refusing to pass `max`. */
static BbStatus push_digit(int64_t *value, char digit, int64_t max) {
    int d = digit - '0';
    if (*value > (max - d) / 10)
        return BB_ERR_RANGE;
    *value = *value * 10 + d;
    return BB_OK;
}

/* Validates comma grouping in the integer part [begin, end): "1,234,567" but not "12,34". */
static int grouping_ok(const char *begin, const char *end) {
    const char *comma = memchr(begin, ',', (size_t)(end - begin));
    if (!comma)
        return 1;
    size_t lead = (size_t)(comma - begin);
    if (lead < 1 || lead > 3)
        return 0;
    for (const char *p = comma; p < end; p += 4) {
        if (*p != ',' || end - p < 4)
            return 0;
        if (!is_digit(p[1]) || !is_digit(p[2]) || !is_digit(p[3]))
            return 0;
    }
    return 1;
}

BbStatus decimal2_parse(const char *text, int64_t max, int64_t *out) {
    if (!text || !out || max < 0)
        return BB_ERR_INVALID_ARG;

    const char *p = text;
    const char *int_begin = p;
    while (is_digit(*p) || *p == ',')
        p++;
    const char *int_end = p;
    if (!grouping_ok(int_begin, int_end))
        return BB_ERR_PARSE;

    int fraction_digits = 0;
    const char *frac = NULL;
    if (*p == '.') {
        frac = ++p;
        while (is_digit(*p))
            p++;
        fraction_digits = (int)(p - frac);
        if (fraction_digits == 0 || fraction_digits > 2)
            return BB_ERR_PARSE;
    }
    if (*p != '\0' || (int_end == int_begin && fraction_digits == 0))
        return BB_ERR_PARSE;

    /* Build the value in cents, checking against max at every step. */
    int64_t value = 0;
    BbStatus rc = BB_OK;
    for (const char *q = int_begin; q < int_end && rc == BB_OK; q++)
        if (*q != ',')
            rc = push_digit(&value, *q, max);
    for (int i = 0; i < 2 && rc == BB_OK; i++)
        rc = push_digit(&value, i < fraction_digits ? frac[i] : '0', max);
    if (rc != BB_OK)
        return rc;

    *out = value;
    return BB_OK;
}

BbStatus money_parse(const char *text, Money *out) {
    return decimal2_parse(text, MONEY_MAX, out);
}

static void format(Money amount, char *buf, size_t size, int grouped) {
    if (!buf || size == 0)
        return;
    /* Work on the magnitude as unsigned so even INT64_MIN is handled. */
    uint64_t mag = amount < 0 ? 0u - (uint64_t)amount : (uint64_t)amount;
    char tmp[MONEY_STR_MAX];
    char *p = tmp + sizeof tmp;
    *--p = '\0';
    *--p = (char)('0' + mag % 10);
    mag /= 10;
    *--p = (char)('0' + mag % 10);
    mag /= 10;
    *--p = '.';
    int digits = 0;
    do {
        if (grouped && digits > 0 && digits % 3 == 0)
            *--p = ',';
        *--p = (char)('0' + mag % 10);
        mag /= 10;
        digits++;
    } while (mag);
    if (amount < 0)
        *--p = '-';

    size_t len = strlen(p);
    if (len >= size)
        len = size - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
}

void money_format(Money amount, char *buf, size_t size) {
    format(amount, buf, size, 1);
}

void money_format_plain(Money amount, char *buf, size_t size) {
    format(amount, buf, size, 0);
}

BbStatus money_add(Money a, Money b, Money *out) {
    if (!out)
        return BB_ERR_INVALID_ARG;
    /* Operands are bounded by MONEY_MAX in practice, but guard the int64 range anyway. */
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return BB_ERR_RANGE;
    Money sum = a + b;
    if (sum > MONEY_MAX || sum < -MONEY_MAX)
        return BB_ERR_RANGE;
    *out = sum;
    return BB_OK;
}
