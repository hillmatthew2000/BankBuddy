#ifndef STATUS_H
#define STATUS_H

/* Result codes shared by every module. BB_OK is zero so `if (rc)` reads as "if failed". */
typedef enum BbStatus {
    BB_OK = 0,
    BB_ERR_INVALID_ARG,  /* programming error: NULL or nonsensical argument */
    BB_ERR_PARSE,        /* user input or file field is malformed */
    BB_ERR_RANGE,        /* well-formed value outside the allowed limits */
    BB_ERR_INSUFFICIENT, /* not enough funds */
    BB_ERR_NOT_FOUND,    /* no such account */
    BB_ERR_NO_MEMORY,    /* allocation failed */
    BB_ERR_IO,           /* file could not be read or written */
    BB_ERR_CORRUPT,      /* data file is damaged */
    BB_ERR_VERSION,      /* data file was written by a newer version */
    BB_ERR_CLOSED,       /* the account has been closed */
    BB_ERR_NOT_EMPTY     /* the account still holds money */
} BbStatus;

/* Short human-readable description, suitable for showing to the user. */
const char *bb_status_message(BbStatus status);

#endif /* STATUS_H */
