/*
 * kiss.c — KISS framing: single-frame encode/decode + streaming decoder
 */

#include <stddef.h>
#include <string.h>
#include "libaprs/kiss.h"

/* ------------------------------------------------------------------ */
/* single-frame encode                                                 */
/* ------------------------------------------------------------------ */

aprs_err_t kiss_encode(
    uint8_t port,
    kiss_cmd_t cmd,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *out,
    size_t outlen,
    size_t *written)
{
    size_t i, pos = 0;
    uint8_t type;

    if (!out || !written) return APRS_ERR_INVALID_ARG;
    if (port > 15) return APRS_ERR_INVALID_ARG;

    type = (uint8_t)((port << 4) | (cmd & 0x0F));

    if (outlen < 3) return APRS_ERR_OVERFLOW;

    out[pos++] = KISS_FEND;
    out[pos++] = type;

    for (i = 0; i < payload_len; i++) {
        if (pos + 2 >= outlen) return APRS_ERR_OVERFLOW;
        if (payload[i] == KISS_FEND) {
            out[pos++] = KISS_FESC;
            out[pos++] = KISS_TFEND;
        } else if (payload[i] == KISS_FESC) {
            out[pos++] = KISS_FESC;
            out[pos++] = KISS_TFESC;
        } else {
            out[pos++] = payload[i];
        }
    }

    if (pos + 1 > outlen) return APRS_ERR_OVERFLOW;
    out[pos++] = KISS_FEND;
    *written = pos;

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* single-frame decode                                                 */
/* ------------------------------------------------------------------ */

aprs_err_t kiss_decode(
    const uint8_t *in,
    size_t inlen,
    uint8_t *port,
    kiss_cmd_t *cmd,
    uint8_t *payload,
    size_t payload_max,
    size_t *payload_len)
{
    size_t i, pos = 0;
    uint8_t type;

    if (!in || inlen < 3 || !port || !cmd || !payload || !payload_len)
        return APRS_ERR_INVALID_ARG;

    if (in[0] != KISS_FEND || in[inlen - 1] != KISS_FEND)
        return APRS_ERR_PARSE;

    type = in[1];
    *port = (uint8_t)((type >> 4) & 0x0F);
    *cmd = (kiss_cmd_t)(type & 0x0F);

    for (i = 2; i < inlen - 1; i++) {
        if (pos >= payload_max) return APRS_ERR_OVERFLOW;

        if (in[i] == KISS_FESC) {
            if (i + 1 >= inlen - 1) return APRS_ERR_PARSE;
            i++;
            if (in[i] == KISS_TFEND)      payload[pos++] = KISS_FEND;
            else if (in[i] == KISS_TFESC)  payload[pos++] = KISS_FESC;
            else return APRS_ERR_PARSE;
        } else {
            payload[pos++] = in[i];
        }
    }

    *payload_len = pos;
    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* streaming decoder                                                   */
/* ------------------------------------------------------------------ */

void kiss_decoder_init(kiss_decoder_t *dec, kiss_frame_cb cb, void *user)
{
    if (!dec) return;
    memset(dec, 0, sizeof(*dec));
    dec->state = KISS_STATE_WAIT_FEND;
    dec->cb = cb;
    dec->cb_user = user;
}

void kiss_decoder_reset(kiss_decoder_t *dec)
{
    if (!dec) return;
    dec->state = KISS_STATE_WAIT_FEND;
    dec->len = 0;
}

static void decoder_emit(kiss_decoder_t *dec)
{
    if (dec->cb && dec->len > 0)
        dec->cb(dec->port, dec->cmd, dec->buf, dec->len, dec->cb_user);
    /* even without callback, reset for next frame */
    dec->len = 0;
    dec->state = KISS_STATE_WAIT_FEND;
}

static void decoder_append(kiss_decoder_t *dec, uint8_t byte)
{
    if (dec->len < KISS_PAYLOAD_MAX)
        dec->buf[dec->len++] = byte;
    /* silently drop if buffer full — frame will be oversized/corrupt
     * but we keep going so we can resync on next FEND */
}

aprs_err_t kiss_decoder_feed(kiss_decoder_t *dec,
                             const uint8_t *data, size_t len)
{
    size_t i;

    if (!dec || (!data && len > 0)) return APRS_ERR_INVALID_ARG;

    for (i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (dec->state) {

        case KISS_STATE_WAIT_FEND:
            if (b == KISS_FEND)
                dec->state = KISS_STATE_WAIT_CMD;
            /* else discard — inter-frame garbage */
            break;

        case KISS_STATE_WAIT_CMD:
            if (b == KISS_FEND) {
                /* consecutive FENDs — stay in this state */
                break;
            }
            dec->port = (uint8_t)((b >> 4) & 0x0F);
            dec->cmd = (kiss_cmd_t)(b & 0x0F);
            dec->len = 0;
            dec->state = KISS_STATE_DATA;
            break;

        case KISS_STATE_DATA:
            if (b == KISS_FEND) {
                /* end of frame */
                decoder_emit(dec);
                /* after emit, state is WAIT_FEND — but this FEND
                 * could also be the start of the next frame */
                dec->state = KISS_STATE_WAIT_CMD;
            } else if (b == KISS_FESC) {
                dec->state = KISS_STATE_ESCAPE;
            } else {
                decoder_append(dec, b);
            }
            break;

        case KISS_STATE_ESCAPE:
            if (b == KISS_TFEND) {
                decoder_append(dec, KISS_FEND);
            } else if (b == KISS_TFESC) {
                decoder_append(dec, KISS_FESC);
            } else {
                /* invalid escape — per KISS spec, discard the
                 * FESC and treat this byte as normal data */
                decoder_append(dec, b);
            }
            dec->state = KISS_STATE_DATA;
            break;
        }
    }

    return APRS_OK;
}
