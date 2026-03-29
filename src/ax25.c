/*
 * ax25.c — AX.25 UI frame encode/decode with real address packing
 *
 * Binary frame layout (no FCS — KISS TNC adds it):
 *   [dst 7B] [src 7B] [repeater 7B * N] [control 1B] [pid 1B] [info ...]
 *
 * Each 7-byte address field:
 *   bytes 0-5: callsign chars shifted left by 1, space-padded to 6
 *   byte 6:    H R R SSID*2 X
 *              H = has-been-repeated / command bit
 *              RR = reserved, set to 1 (0x60)
 *              SSID = 0-15 in bits 4-1
 *              X = extension bit (1 = last address)
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "libaprs/ax25.h"
#include "libaprs/aprs.h"

void ax25_ui_frame_init(ax25_ui_frame_t *f)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->control = AX25_CTRL_UI;
    f->pid = AX25_PID_NOLAYER3;
}

/* ------------------------------------------------------------------ */
/* address pack / unpack                                               */
/* ------------------------------------------------------------------ */

/*
 * Parse "N0CALL-9" or "N0CALL" into base callsign (up to 6 chars)
 * and SSID (0-15).  Strips trailing '*' (H-bit marker) before parsing.
 */
static aprs_err_t split_callsign(const char *text, char base[7],
                                 int *ssid, bool *has_star)
{
    const char *dash;
    size_t len;
    char clean[APRS_CALLSIGN_MAX];

    if (!text || !text[0]) return APRS_ERR_INVALID_ARG;

    /* copy and strip trailing * */
    len = strlen(text);
    if (len >= sizeof(clean)) return APRS_ERR_INVALID_ARG;
    memcpy(clean, text, len + 1);

    *has_star = false;
    if (len > 0 && clean[len - 1] == '*') {
        *has_star = true;
        clean[len - 1] = '\0';
        len--;
    }

    dash = strchr(clean, '-');
    if (dash) {
        size_t blen = (size_t)(dash - clean);
        if (blen > AX25_CALL_LEN || blen == 0) return APRS_ERR_INVALID_ARG;
        memcpy(base, clean, blen);
        base[blen] = '\0';
        *ssid = atoi(dash + 1);
        if (*ssid < 0 || *ssid > 15) return APRS_ERR_INVALID_ARG;
    } else {
        if (len > AX25_CALL_LEN) return APRS_ERR_INVALID_ARG;
        memcpy(base, clean, len + 1);
        *ssid = 0;
    }

    return APRS_OK;
}

aprs_err_t ax25_pack_address(const char *callsign, uint8_t *out,
                             bool last, bool hbit)
{
    char base[7];
    int ssid;
    bool has_star;
    size_t i, blen;
    aprs_err_t rc;

    if (!callsign || !out) return APRS_ERR_INVALID_ARG;

    rc = split_callsign(callsign, base, &ssid, &has_star);
    if (rc != APRS_OK) return rc;

    /* if the text had a trailing *, treat as H-bit set */
    if (has_star) hbit = true;

    blen = strlen(base);

    /* pack callsign chars, shifted left by 1, space-padded */
    for (i = 0; i < AX25_CALL_LEN; i++) {
        char c = (i < blen) ? (char)toupper((unsigned char)base[i]) : ' ';
        out[i] = (uint8_t)(c << 1);
    }

    /* SSID byte: H RR SSID X */
    out[6] = (uint8_t)(0x60                    /* reserved bits */
                       | ((ssid & 0x0F) << 1)  /* SSID in bits 4-1 */
                       | (last ? 0x01 : 0x00)  /* extension bit */
                       | (hbit ? 0x80 : 0x00));/* H bit */

    return APRS_OK;
}

aprs_err_t ax25_unpack_address(const uint8_t *in, char *out, bool *hbit)
{
    int i, ssid, end;
    char base[7];

    if (!in || !out) return APRS_ERR_INVALID_ARG;

    /* extract callsign chars (shifted right by 1), trim spaces */
    end = AX25_CALL_LEN;
    for (i = 0; i < AX25_CALL_LEN; i++) {
        base[i] = (char)(in[i] >> 1);
        if (base[i] == ' ' && end == AX25_CALL_LEN)
            end = i;
    }
    if (end == AX25_CALL_LEN) {
        /* no trailing spaces — find last non-space */
        while (end > 0 && base[end - 1] == ' ')
            end--;
    }
    base[end] = '\0';

    ssid = (in[6] >> 1) & 0x0F;

    if (hbit)
        *hbit = (in[6] & 0x80) != 0;

    if (ssid > 0)
        snprintf(out, APRS_CALLSIGN_MAX, "%s-%d", base, ssid);
    else
        snprintf(out, APRS_CALLSIGN_MAX, "%s", base);

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* UI frame encode                                                     */
/* ------------------------------------------------------------------ */

aprs_err_t ax25_encode_ui_frame(
    const ax25_ui_frame_t *frame,
    uint8_t *out,
    size_t outlen,
    size_t *written)
{
    size_t pos = 0;
    size_t need;
    size_t naddr;
    aprs_err_t rc;

    if (!frame || !out || !written) return APRS_ERR_INVALID_ARG;

    naddr = 2 + frame->path_len;
    need = naddr * AX25_ADDR_LEN + 2 + frame->info_len; /* addrs + ctrl + pid + info */
    if (need > outlen) return APRS_ERR_OVERFLOW;

    /* destination — hbit = command bit, for APRS usually 1 */
    rc = ax25_pack_address(frame->dst.callsign, out + pos,
                           naddr == 2 && frame->path_len == 0 ? false : false,
                           true);
    if (rc != APRS_OK) return rc;
    pos += AX25_ADDR_LEN;

    /* source */
    {
        bool src_last = (frame->path_len == 0);
        rc = ax25_pack_address(frame->src.callsign, out + pos,
                               src_last, false);
        if (rc != APRS_OK) return rc;
        pos += AX25_ADDR_LEN;
    }

    /* repeater path */
    {
        size_t i;
        for (i = 0; i < frame->path_len; i++) {
            bool is_last = (i == frame->path_len - 1);
            rc = ax25_pack_address(frame->path[i].callsign, out + pos,
                                   is_last, false);
            if (rc != APRS_OK) return rc;
            pos += AX25_ADDR_LEN;
        }
    }

    /* control + PID */
    out[pos++] = frame->control;
    out[pos++] = frame->pid;

    /* info field */
    if (frame->info_len > 0)
        memcpy(out + pos, frame->info, frame->info_len);
    pos += frame->info_len;

    *written = pos;
    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* UI frame decode                                                     */
/* ------------------------------------------------------------------ */

aprs_err_t ax25_decode_ui_frame(
    const uint8_t *data,
    size_t len,
    ax25_ui_frame_t *out)
{
    size_t pos = 0;
    aprs_err_t rc;

    if (!data || !out) return APRS_ERR_INVALID_ARG;

    ax25_ui_frame_init(out);

    /* minimum: dst(7) + src(7) + control(1) + pid(1) = 16 */
    if (len < 16) return APRS_ERR_PARSE;

    /* destination */
    rc = ax25_unpack_address(data + pos, out->dst.callsign, NULL);
    if (rc != APRS_OK) return rc;

    /* check extension bit — if set, only one address (invalid for UI) */
    if (data[pos + 6] & 0x01) return APRS_ERR_PARSE;
    pos += AX25_ADDR_LEN;

    /* source */
    {
        bool hbit;
        rc = ax25_unpack_address(data + pos, out->src.callsign, &hbit);
        if (rc != APRS_OK) return rc;
    }

    if (data[pos + 6] & 0x01) {
        /* source is last address */
        pos += AX25_ADDR_LEN;
    } else {
        pos += AX25_ADDR_LEN;

        /* repeater addresses */
        out->path_len = 0;
        while (pos + AX25_ADDR_LEN <= len) {
            bool hbit = false;
            bool is_last;

            if (out->path_len >= APRS_PATH_MAX)
                return APRS_ERR_OVERFLOW;

            rc = ax25_unpack_address(data + pos,
                                     out->path[out->path_len].callsign,
                                     &hbit);
            if (rc != APRS_OK) return rc;

            /* if H bit is set, append * to indicate has-been-repeated */
            if (hbit) {
                size_t slen = strlen(out->path[out->path_len].callsign);
                if (slen + 1 < APRS_CALLSIGN_MAX) {
                    out->path[out->path_len].callsign[slen] = '*';
                    out->path[out->path_len].callsign[slen + 1] = '\0';
                }
            }

            is_last = (data[pos + 6] & 0x01) != 0;
            out->path_len++;
            pos += AX25_ADDR_LEN;

            if (is_last) break;
        }
    }

    /* control + PID */
    if (pos + 2 > len) return APRS_ERR_PARSE;
    out->control = data[pos++];
    out->pid = data[pos++];

    /* verify UI frame */
    if (out->control != AX25_CTRL_UI)
        return APRS_ERR_UNSUPPORTED;

    /* info field — remainder */
    out->info_len = len - pos;
    if (out->info_len > APRS_INFO_MAX)
        return APRS_ERR_OVERFLOW;
    if (out->info_len > 0)
        memcpy(out->info, data + pos, out->info_len);

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* FCS-16                                                              */
/* ------------------------------------------------------------------ */

uint16_t ax25_fcs16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    size_t i;
    int j;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0x8408);
            else crc >>= 1;
        }
    }

    return (uint16_t)~crc;
}

/* ------------------------------------------------------------------ */
/* aprs_packet_t <-> ax25_ui_frame_t conversion                        */
/* ------------------------------------------------------------------ */

aprs_err_t ax25_from_aprs(const aprs_packet_t *pkt, ax25_ui_frame_t *frame)
{
    size_t i, ilen;

    if (!pkt || !frame) return APRS_ERR_INVALID_ARG;

    ax25_ui_frame_init(frame);

    memcpy(&frame->dst, &pkt->dst, sizeof(aprs_address_t));
    memcpy(&frame->src, &pkt->src, sizeof(aprs_address_t));

    frame->path_len = pkt->path_len;
    for (i = 0; i < pkt->path_len; i++)
        memcpy(&frame->path[i], &pkt->path[i], sizeof(aprs_address_t));

    ilen = strlen(pkt->raw_info);
    if (ilen > APRS_INFO_MAX) return APRS_ERR_OVERFLOW;
    memcpy(frame->info, pkt->raw_info, ilen);
    frame->info_len = ilen;

    return APRS_OK;
}

aprs_err_t ax25_to_aprs(const ax25_ui_frame_t *frame, aprs_packet_t *pkt)
{
    char tnc2[1024];
    int n, total = 0;
    size_t i, remain;

    if (!frame || !pkt) return APRS_ERR_INVALID_ARG;

    /* build TNC2 line: SRC>DST,PATH:INFO */
    n = snprintf(tnc2, sizeof(tnc2), "%s>%s",
                 frame->src.callsign, frame->dst.callsign);
    if (n < 0 || (size_t)n >= sizeof(tnc2)) return APRS_ERR_OVERFLOW;
    total = n;

    for (i = 0; i < frame->path_len; i++) {
        remain = sizeof(tnc2) - (size_t)total;
        n = snprintf(tnc2 + total, remain, ",%s",
                     frame->path[i].callsign);
        if (n < 0 || (size_t)n >= remain) return APRS_ERR_OVERFLOW;
        total += n;
    }

    /* add colon + info (info may contain non-printable bytes but
     * for APRS over AX.25 it's always printable ASCII) */
    remain = sizeof(tnc2) - (size_t)total;
    if (frame->info_len + 2 > remain) return APRS_ERR_OVERFLOW;
    tnc2[total++] = ':';
    memcpy(tnc2 + total, frame->info, frame->info_len);
    total += (int)frame->info_len;
    tnc2[total] = '\0';

    return aprs_parse_tnc2(tnc2, pkt);
}
