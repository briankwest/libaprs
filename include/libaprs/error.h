#ifndef LIBAPRS_ERROR_H
#define LIBAPRS_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APRS_OK = 0,
    APRS_ERR_INVALID_ARG = -1,
    APRS_ERR_PARSE = -2,
    APRS_ERR_FORMAT = -3,
    APRS_ERR_OVERFLOW = -4,
    APRS_ERR_BAD_CHECKSUM = -5,
    APRS_ERR_UNSUPPORTED = -6,
    APRS_ERR_IO = -7,
    APRS_ERR_NOMEM = -8,
    APRS_ERR_STATE = -9
} aprs_err_t;

const char *aprs_strerror(aprs_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_ERROR_H */
