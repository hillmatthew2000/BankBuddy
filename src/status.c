#include "status.h"

const char *bb_status_message(BbStatus status) {
    switch (status) {
        case BB_OK:
            return "OK";
        case BB_ERR_INVALID_ARG:
            return "Internal error: invalid argument";
        case BB_ERR_PARSE:
            return "Invalid input";
        case BB_ERR_RANGE:
            return "Value is out of range";
        case BB_ERR_INSUFFICIENT:
            return "Insufficient funds";
        case BB_ERR_NOT_FOUND:
            return "Account not found";
        case BB_ERR_NO_MEMORY:
            return "Out of memory";
        case BB_ERR_IO:
            return "Could not read or write the data file";
        case BB_ERR_CORRUPT:
            return "The data file is damaged";
        case BB_ERR_VERSION:
            return "The data file was written by a newer version of BankBuddy";
    }
    return "Unknown error";
}
