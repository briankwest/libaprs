#include "libaprs/error.h"

const char *aprs_strerror(aprs_err_t err) {
    switch (err) {
        case APRS_OK: return "ok";
        case APRS_ERR_INVALID_ARG: return "invalid argument";
        case APRS_ERR_PARSE: return "parse error";
        case APRS_ERR_FORMAT: return "format error";
        case APRS_ERR_OVERFLOW: return "buffer overflow";
        case APRS_ERR_BAD_CHECKSUM: return "bad checksum";
        case APRS_ERR_UNSUPPORTED: return "unsupported";
        case APRS_ERR_IO: return "i/o error";
        case APRS_ERR_NOMEM: return "out of memory";
        case APRS_ERR_STATE: return "invalid state";
        default: return "unknown error";
    }
}
