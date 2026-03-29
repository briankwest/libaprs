#ifndef LIBAPRS_KISS_H
#define LIBAPRS_KISS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KISS_FEND   0xC0
#define KISS_FESC   0xDB
#define KISS_TFEND  0xDC
#define KISS_TFESC  0xDD

/* max payload a KISS frame can carry (AX.25 frame) */
#define KISS_PAYLOAD_MAX  1024

typedef enum {
    KISS_CMD_DATA_FRAME = 0x00,
    KISS_CMD_TXDELAY = 0x01,
    KISS_CMD_PERSIST = 0x02,
    KISS_CMD_SLOTTIME = 0x03,
    KISS_CMD_TXTAIL = 0x04,
    KISS_CMD_FULLDUPLEX = 0x05,
    KISS_CMD_SETHARDWARE = 0x06,
    KISS_CMD_RETURN = 0xFF
} kiss_cmd_t;

/* ------------------------------------------------------------------ */
/* single-frame encode / decode (unchanged API)                        */
/* ------------------------------------------------------------------ */

aprs_err_t kiss_encode(
    uint8_t port,
    kiss_cmd_t cmd,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *out,
    size_t outlen,
    size_t *written
);

aprs_err_t kiss_decode(
    const uint8_t *in,
    size_t inlen,
    uint8_t *port,
    kiss_cmd_t *cmd,
    uint8_t *payload,
    size_t payload_max,
    size_t *payload_len
);

/* ------------------------------------------------------------------ */
/* streaming decoder — feed arbitrary byte chunks, get frame callbacks */
/* ------------------------------------------------------------------ */

/*
 * Callback invoked when a complete KISS frame payload is available.
 *   port        — KISS port number (0-15)
 *   cmd         — KISS command byte
 *   payload     — decoded payload (escapes already resolved)
 *   payload_len — payload length in bytes
 *   user        — opaque user pointer
 */
typedef void (*kiss_frame_cb)(uint8_t port, kiss_cmd_t cmd,
                              const uint8_t *payload, size_t payload_len,
                              void *user);

typedef enum {
    KISS_STATE_WAIT_FEND,   /* haven't seen opening FEND yet */
    KISS_STATE_WAIT_CMD,    /* got FEND, waiting for type byte */
    KISS_STATE_DATA,        /* receiving payload bytes */
    KISS_STATE_ESCAPE       /* got FESC, next byte is escaped */
} kiss_state_t;

typedef struct {
    kiss_state_t state;
    uint8_t port;
    kiss_cmd_t cmd;
    uint8_t buf[KISS_PAYLOAD_MAX];
    size_t len;
    kiss_frame_cb cb;
    void *cb_user;
} kiss_decoder_t;

/*
 * Initialize a streaming KISS decoder.
 *   cb   — called for each complete frame
 *   user — passed through to callback
 */
void kiss_decoder_init(kiss_decoder_t *dec, kiss_frame_cb cb, void *user);

/* Reset decoder state (e.g. after error or reconnect) */
void kiss_decoder_reset(kiss_decoder_t *dec);

/*
 * Feed raw bytes into the decoder.  May invoke the callback zero
 * or more times if complete frames are found in the data.
 * Returns APRS_OK on success, APRS_ERR_INVALID_ARG on NULL.
 */
aprs_err_t kiss_decoder_feed(kiss_decoder_t *dec,
                             const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_KISS_H */
