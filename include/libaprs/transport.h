#ifndef LIBAPRS_TRANSPORT_H
#define LIBAPRS_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aprs_transport aprs_transport_t;

typedef aprs_err_t (*aprs_transport_open_fn)(aprs_transport_t *t);
typedef aprs_err_t (*aprs_transport_close_fn)(aprs_transport_t *t);
typedef aprs_err_t (*aprs_transport_read_fn)(aprs_transport_t *t, uint8_t *buf,
                                             size_t maxlen, size_t *nread);
typedef aprs_err_t (*aprs_transport_write_fn)(aprs_transport_t *t,
                                              const uint8_t *buf, size_t len,
                                              size_t *nwritten);

struct aprs_transport {
    void *ctx;
    aprs_transport_open_fn open;
    aprs_transport_close_fn close;
    aprs_transport_read_fn read;
    aprs_transport_write_fn write;
};

aprs_err_t aprs_transport_open(aprs_transport_t *t);
aprs_err_t aprs_transport_close(aprs_transport_t *t);
aprs_err_t aprs_transport_read(aprs_transport_t *t, uint8_t *buf,
                                size_t maxlen, size_t *nread);
aprs_err_t aprs_transport_write(aprs_transport_t *t, const uint8_t *buf,
                                 size_t len, size_t *nwritten);

/* ------------------------------------------------------------------ */
/* serial transport                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *device;   /* e.g. "/dev/ttyUSB0" */
    int baud;             /* e.g. 9600, 1200 */
} aprs_serial_opts_t;

/*
 * Create a serial transport.  Caller must eventually call
 * aprs_transport_close() and aprs_transport_serial_destroy().
 */
aprs_err_t aprs_transport_serial_create(aprs_transport_t *t,
                                        const aprs_serial_opts_t *opts);
void aprs_transport_serial_destroy(aprs_transport_t *t);

/* ------------------------------------------------------------------ */
/* TCP transport                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *host;
    uint16_t port;
} aprs_tcp_opts_t;

/*
 * Create a TCP transport.  Caller must eventually call
 * aprs_transport_close() and aprs_transport_tcp_destroy().
 */
aprs_err_t aprs_transport_tcp_create(aprs_transport_t *t,
                                     const aprs_tcp_opts_t *opts);
void aprs_transport_tcp_destroy(aprs_transport_t *t);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_TRANSPORT_H */
