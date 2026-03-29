#ifndef LIBAPRS_APRSIS_H
#define LIBAPRS_APRSIS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* max line length on APRS-IS (spec says 512 but be generous) */
#define APRSIS_LINE_MAX   1024
/* default APRS-IS port */
#define APRSIS_PORT       14580
/* dedup ring size — number of recent packet hashes to remember */
#define APRSIS_DEDUP_SLOTS 128

typedef struct aprsis_client aprsis_client_t;

/*
 * Callback invoked for each received APRS-IS line.
 * Server comment lines (starting with #) are filtered unless
 * the user opts in with aprsis_set_pass_comments().
 */
typedef void (*aprsis_line_cb)(const char *line, void *user);

typedef struct {
    const char *host;       /* server hostname, e.g. "rotate.aprs2.net" */
    uint16_t port;          /* default 14580 */
    const char *login;      /* callsign, e.g. "N0CALL" */
    const char *passcode;   /* APRS-IS passcode, "-1" for RO */
    const char *filter;     /* server filter string, NULL for none */
} aprsis_connect_params_t;

/* --- lifecycle --- */
aprsis_client_t *aprsis_client_create(void);
void aprsis_client_destroy(aprsis_client_t *c);

/* --- connection --- */
aprs_err_t aprsis_connect(aprsis_client_t *c, const aprsis_connect_params_t *p);
aprs_err_t aprsis_disconnect(aprsis_client_t *c);
bool       aprsis_is_connected(const aprsis_client_t *c);

/*
 * Send a raw line to the server (CRLF appended automatically).
 */
aprs_err_t aprsis_send_line(aprsis_client_t *c, const char *line);

/*
 * Read one line from the server (blocking).  Strips CRLF.
 * Returns APRS_OK with *nread=0 on timeout with no data.
 * Returns APRS_ERR_IO on disconnect.
 */
aprs_err_t aprsis_read_line(aprsis_client_t *c, char *buf, size_t buflen,
                            size_t *nread);

/*
 * Set the line callback for aprsis_run().
 */
void aprsis_set_line_callback(aprsis_client_t *c, aprsis_line_cb cb,
                              void *user);

/*
 * By default, server comment lines (# ...) are silently consumed.
 * Call this with true to pass them through to the callback / read_line.
 */
void aprsis_set_pass_comments(aprsis_client_t *c, bool pass);

/*
 * Blocking receive loop — reads lines and invokes the callback until
 * the connection drops or aprsis_stop() is called from within the
 * callback (or another thread).  Returns the error that ended the loop.
 */
aprs_err_t aprsis_run(aprsis_client_t *c);

/*
 * Signal the run loop to stop after the current line.
 */
void aprsis_stop(aprsis_client_t *c);

/* --- duplicate suppression --- */

typedef struct {
    uint32_t hashes[APRSIS_DEDUP_SLOTS];
    int head;
    int count;
} aprsis_dedup_t;

void aprsis_dedup_init(aprsis_dedup_t *d);

/*
 * Check if a line was seen recently.
 * Returns true if this is a duplicate (already in the ring).
 * If not a duplicate, adds it to the ring and returns false.
 */
bool aprsis_dedup_check(aprsis_dedup_t *d, const char *line);

/* --- APRS-IS passcode computation --- */

/*
 * Compute the APRS-IS passcode for a callsign (strips SSID).
 * Returns the numeric passcode as an int.
 */
int aprsis_passcode(const char *callsign);

/*
 * Format the APRS-IS login line into buf.
 * Returns APRS_OK or APRS_ERR_OVERFLOW.
 */
aprs_err_t aprsis_format_login(const aprsis_connect_params_t *p,
                               char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_APRSIS_H */
