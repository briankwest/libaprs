#ifndef LIBAPRS_AX25_H
#define LIBAPRS_AX25_H

#include <stddef.h>
#include <stdint.h>
#include "error.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AX.25 address field is always 7 bytes */
#define AX25_ADDR_LEN     7
/* Max callsign chars before SSID (6 chars in AX.25) */
#define AX25_CALL_LEN     6
/* Max binary frame: 7*(2+8 repeaters) + control + pid + info + fcs */
#define AX25_FRAME_MAX    (AX25_ADDR_LEN * (2 + APRS_PATH_MAX) + 2 + APRS_INFO_MAX + 2)

/* Control and PID for UI frames */
#define AX25_CTRL_UI      0x03
#define AX25_PID_NOLAYER3 0xF0

typedef struct {
    aprs_address_t dst;
    aprs_address_t src;
    aprs_address_t path[APRS_PATH_MAX];
    size_t path_len;
    uint8_t control;
    uint8_t pid;
    uint8_t info[APRS_INFO_MAX];
    size_t info_len;
} ax25_ui_frame_t;

void ax25_ui_frame_init(ax25_ui_frame_t *f);

/*
 * Pack a text callsign ("N0CALL-9") into a 7-byte AX.25 address field.
 *   out      — must point to AX25_ADDR_LEN (7) bytes
 *   last     — set the extension bit (1 = last address in header)
 *   hbit     — set the H bit (has-been-repeated / command-response)
 */
aprs_err_t ax25_pack_address(const char *callsign, uint8_t *out,
                             bool last, bool hbit);

/*
 * Unpack a 7-byte AX.25 address field to text callsign ("N0CALL-9").
 *   in       — pointer to 7 bytes
 *   out      — text buffer, must be >= APRS_CALLSIGN_MAX bytes
 *   hbit     — if non-NULL, receives the H bit value
 */
aprs_err_t ax25_unpack_address(const uint8_t *in, char *out, bool *hbit);

/*
 * Encode an ax25_ui_frame_t to a binary AX.25 UI frame (without FCS).
 * The output is suitable for wrapping in a KISS data frame.
 */
aprs_err_t ax25_encode_ui_frame(
    const ax25_ui_frame_t *frame,
    uint8_t *out,
    size_t outlen,
    size_t *written
);

/*
 * Decode a binary AX.25 UI frame (without FCS) into ax25_ui_frame_t.
 */
aprs_err_t ax25_decode_ui_frame(
    const uint8_t *data,
    size_t len,
    ax25_ui_frame_t *out
);

/* CRC-16 used by AX.25 (CCITT polynomial 0x8408, bit-reversed) */
uint16_t ax25_fcs16(const uint8_t *data, size_t len);

/*
 * Convert an aprs_packet_t to an ax25_ui_frame_t.
 * Copies addresses and raw_info into the frame structure.
 */
aprs_err_t ax25_from_aprs(const aprs_packet_t *pkt, ax25_ui_frame_t *frame);

/*
 * Convert an ax25_ui_frame_t to an aprs_packet_t.
 * Builds a TNC2-style line internally and parses it, so the
 * resulting packet has all structured fields populated.
 */
aprs_err_t ax25_to_aprs(const ax25_ui_frame_t *frame, aprs_packet_t *pkt);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_AX25_H */
