/*
 * test_ax25.c — AX.25 address packing, UI frame encode/decode, FCS,
 *               APRS conversion, and binary test vectors
 */

#include <string.h>
#include <stdio.h>
#include "libaprs/ax25.h"
#include "libaprs/aprs.h"
#include "libaprs/kiss.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

void test_ax25(void)
{
    ax25_ui_frame_t frame;
    uint8_t buf[AX25_FRAME_MAX];
    size_t written;
    aprs_err_t rc;

    /* ------------------------------------------------------------------ */
    /* init                                                                */
    /* ------------------------------------------------------------------ */

    test_begin("ax25_ui_frame_init defaults");
    ax25_ui_frame_init(&frame);
    test_assert(frame.control == AX25_CTRL_UI, "control should be 0x03");
    test_assert(frame.pid == AX25_PID_NOLAYER3, "pid should be 0xF0");
    test_assert(frame.info_len == 0, "info_len should be 0");
    test_assert(frame.path_len == 0, "path_len should be 0");
    test_end();

    /* ------------------------------------------------------------------ */
    /* address pack / unpack                                               */
    /* ------------------------------------------------------------------ */

    test_begin("pack address N0CALL (no SSID)");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("N0CALL", addr, false, false);
        test_assert(rc == APRS_OK, "pack failed");
        /* 'N'=0x4E, shifted: 0x9C */
        test_assert(addr[0] == ('N' << 1), "byte 0 wrong");
        /* '0'=0x30, shifted: 0x60 */
        test_assert(addr[1] == ('0' << 1), "byte 1 wrong");
        test_assert(addr[2] == ('C' << 1), "byte 2 wrong");
        test_assert(addr[3] == ('A' << 1), "byte 3 wrong");
        test_assert(addr[4] == ('L' << 1), "byte 4 wrong");
        test_assert(addr[5] == ('L' << 1), "byte 5 wrong");
        /* SSID=0, no extension, no H: 0x60 */
        test_assert(addr[6] == 0x60, "ssid byte wrong");
    }
    test_end();

    test_begin("pack address N0CALL-9");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("N0CALL-9", addr, false, false);
        test_assert(rc == APRS_OK, "pack failed");
        /* SSID=9: 0x60 | (9<<1) = 0x60 | 0x12 = 0x72 */
        test_assert(addr[6] == 0x72, "ssid byte wrong");
    }
    test_end();

    test_begin("pack address with extension bit");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("N0CALL-9", addr, true, false);
        test_assert(rc == APRS_OK, "pack failed");
        /* 0x72 | 0x01 = 0x73 */
        test_assert(addr[6] == 0x73, "extension bit not set");
    }
    test_end();

    test_begin("pack address with H bit");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("N0CALL", addr, false, true);
        test_assert(rc == APRS_OK, "pack failed");
        /* 0x60 | 0x80 = 0xE0 */
        test_assert(addr[6] == 0xE0, "H bit not set");
    }
    test_end();

    test_begin("pack short callsign (AB1) pads with spaces");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("AB1", addr, false, false);
        test_assert(rc == APRS_OK, "pack failed");
        test_assert(addr[0] == ('A' << 1), "byte 0 wrong");
        test_assert(addr[1] == ('B' << 1), "byte 1 wrong");
        test_assert(addr[2] == ('1' << 1), "byte 2 wrong");
        test_assert(addr[3] == (' ' << 1), "byte 3 should be space");
        test_assert(addr[4] == (' ' << 1), "byte 4 should be space");
        test_assert(addr[5] == (' ' << 1), "byte 5 should be space");
    }
    test_end();

    test_begin("pack address with star sets H bit");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("RELAY*", addr, false, false);
        test_assert(rc == APRS_OK, "pack failed");
        /* H bit should be set from the star */
        test_assert((addr[6] & 0x80) != 0, "H bit not set from star");
        /* callsign should be RELAY, not RELAY* */
        test_assert(addr[0] == ('R' << 1), "byte 0 wrong");
    }
    test_end();

    test_begin("unpack address round-trip");
    {
        uint8_t addr[7];
        char text[APRS_CALLSIGN_MAX];
        bool hbit;

        rc = ax25_pack_address("W3ADO-1", addr, false, false);
        test_assert(rc == APRS_OK, "pack failed");
        rc = ax25_unpack_address(addr, text, &hbit);
        test_assert(rc == APRS_OK, "unpack failed");
        test_assert(strcmp(text, "W3ADO-1") == 0, "round-trip mismatch");
        test_assert(hbit == false, "H bit should be false");
    }
    test_end();

    test_begin("unpack address with H bit");
    {
        uint8_t addr[7];
        char text[APRS_CALLSIGN_MAX];
        bool hbit;

        rc = ax25_pack_address("WIDE1-1", addr, false, true);
        test_assert(rc == APRS_OK, "pack failed");
        rc = ax25_unpack_address(addr, text, &hbit);
        test_assert(rc == APRS_OK, "unpack failed");
        test_assert(strcmp(text, "WIDE1-1") == 0, "callsign wrong");
        test_assert(hbit == true, "H bit should be true");
    }
    test_end();

    test_begin("unpack SSID 0 has no dash");
    {
        uint8_t addr[7];
        char text[APRS_CALLSIGN_MAX];

        rc = ax25_pack_address("APRS", addr, false, false);
        test_assert(rc == APRS_OK, "pack failed");
        rc = ax25_unpack_address(addr, text, NULL);
        test_assert(rc == APRS_OK, "unpack failed");
        test_assert(strcmp(text, "APRS") == 0, "should not have -0");
    }
    test_end();

    test_begin("pack address rejects too-long callsign");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("TOOLONG-1", addr, false, false);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject >6 char base");
    }
    test_end();

    test_begin("pack address rejects SSID > 15");
    {
        uint8_t addr[7];
        rc = ax25_pack_address("N0CALL-16", addr, false, false);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject SSID 16");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* UI frame encode / decode                                            */
    /* ------------------------------------------------------------------ */

    test_begin("encode minimal UI frame (no path, no info)");
    {
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        frame.path_len = 0;
        frame.info_len = 0;

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");
        /* 7 dst + 7 src + 1 ctrl + 1 pid = 16 bytes */
        test_assert(written == 16, "wrong frame size");
        /* control byte */
        test_assert(buf[14] == AX25_CTRL_UI, "control wrong");
        /* PID byte */
        test_assert(buf[15] == AX25_PID_NOLAYER3, "PID wrong");
        /* source address should have extension bit set (last addr) */
        test_assert((buf[13] & 0x01) == 1, "src ext bit not set");
        /* destination should NOT have extension bit */
        test_assert((buf[6] & 0x01) == 0, "dst ext bit should be 0");
    }
    test_end();

    test_begin("encode UI frame with path and info");
    {
        const char *info = "!4903.50N/07201.75W-Test";
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL-9");
        snprintf(frame.path[0].callsign,
                 sizeof(frame.path[0].callsign), "WIDE1-1");
        snprintf(frame.path[1].callsign,
                 sizeof(frame.path[1].callsign), "WIDE2-1");
        frame.path_len = 2;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");
        /* 7*4 addrs + 2 (ctrl+pid) + info_len */
        test_assert(written == 28 + 2 + strlen(info), "wrong frame size");
        /* last path address should have extension bit */
        test_assert((buf[27] & 0x01) == 1, "last repeater ext bit not set");
        /* source should NOT have ext bit (repeaters follow) */
        test_assert((buf[13] & 0x01) == 0, "src ext bit should be 0");
    }
    test_end();

    test_begin("decode UI frame round-trip (no path)");
    {
        ax25_ui_frame_t out;
        const char *info = ">status text";

        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "W3ADO-1");
        frame.path_len = 0;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");

        rc = ax25_decode_ui_frame(buf, written, &out);
        test_assert(rc == APRS_OK, "decode failed");
        test_assert(strcmp(out.dst.callsign, "APRS") == 0, "dst wrong");
        test_assert(strcmp(out.src.callsign, "W3ADO-1") == 0, "src wrong");
        test_assert(out.path_len == 0, "path_len wrong");
        test_assert(out.info_len == strlen(info), "info_len wrong");
        test_assert(memcmp(out.info, info, strlen(info)) == 0, "info wrong");
        test_assert(out.control == AX25_CTRL_UI, "control wrong");
        test_assert(out.pid == AX25_PID_NOLAYER3, "pid wrong");
    }
    test_end();

    test_begin("decode UI frame round-trip (with path)");
    {
        ax25_ui_frame_t out;
        const char *info = "!4903.50N/07201.75W-Test";

        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APZ001");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL-9");
        snprintf(frame.path[0].callsign,
                 sizeof(frame.path[0].callsign), "WIDE1-1");
        snprintf(frame.path[1].callsign,
                 sizeof(frame.path[1].callsign), "WIDE2-1");
        frame.path_len = 2;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");

        rc = ax25_decode_ui_frame(buf, written, &out);
        test_assert(rc == APRS_OK, "decode failed");
        test_assert(strcmp(out.dst.callsign, "APZ001") == 0, "dst wrong");
        test_assert(strcmp(out.src.callsign, "N0CALL-9") == 0, "src wrong");
        test_assert(out.path_len == 2, "path_len wrong");
        test_assert(strcmp(out.path[0].callsign, "WIDE1-1") == 0,
                    "path[0] wrong");
        test_assert(strcmp(out.path[1].callsign, "WIDE2-1") == 0,
                    "path[1] wrong");
        test_assert(out.info_len == strlen(info), "info_len wrong");
        test_assert(memcmp(out.info, info, strlen(info)) == 0, "info wrong");
    }
    test_end();

    test_begin("decode preserves H-bit as * on path");
    {
        ax25_ui_frame_t out;
        const char *info = ">test";

        /* Encode with WIDE1-1 having H bit set (has-been-repeated) */
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        snprintf(frame.path[0].callsign,
                 sizeof(frame.path[0].callsign), "WIDE1-1*");
        snprintf(frame.path[1].callsign,
                 sizeof(frame.path[1].callsign), "WIDE2-1");
        frame.path_len = 2;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");

        /* verify H bit is set in the binary for path[0] */
        /* path[0] starts at offset 14 (7 dst + 7 src) */
        test_assert((buf[14 + 6] & 0x80) != 0, "H bit not in binary");
        /* path[1] should NOT have H bit */
        test_assert((buf[21 + 6] & 0x80) == 0, "path[1] H bit should be 0");

        rc = ax25_decode_ui_frame(buf, written, &out);
        test_assert(rc == APRS_OK, "decode failed");
        test_assert(strcmp(out.path[0].callsign, "WIDE1-1*") == 0,
                    "path[0] should have *");
        test_assert(strcmp(out.path[1].callsign, "WIDE2-1") == 0,
                    "path[1] should not have *");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* binary test vector — hand-crafted known-good AX.25 frame           */
    /* ------------------------------------------------------------------ */

    test_begin("decode known binary AX.25 frame");
    {
        /*
         * N0CALL-9>APRS:>Hello
         * dst: APRS   (no SSID) — 'A'<<1,'P'<<1,'R'<<1,'S'<<1,' '<<1,' '<<1, 0x60
         * src: N0CALL-9          — 'N'<<1,'0'<<1,'C'<<1,'A'<<1,'L'<<1,'L'<<1, 0x60|(9<<1)|0x01
         * control: 0x03, PID: 0xF0
         * info: ">Hello"
         */
        uint8_t known[] = {
            /* dst: APRS */
            'A'<<1, 'P'<<1, 'R'<<1, 'S'<<1, ' '<<1, ' '<<1, 0x60,
            /* src: N0CALL-9 (last address) */
            'N'<<1, '0'<<1, 'C'<<1, 'A'<<1, 'L'<<1, 'L'<<1, 0x60|(9<<1)|0x01,
            /* control, PID */
            0x03, 0xF0,
            /* info: ">Hello" */
            '>', 'H', 'e', 'l', 'l', 'o'
        };
        ax25_ui_frame_t out;

        rc = ax25_decode_ui_frame(known, sizeof(known), &out);
        test_assert(rc == APRS_OK, "decode failed");
        test_assert(strcmp(out.dst.callsign, "APRS") == 0, "dst wrong");
        test_assert(strcmp(out.src.callsign, "N0CALL-9") == 0, "src wrong");
        test_assert(out.path_len == 0, "path_len wrong");
        test_assert(out.info_len == 6, "info_len wrong");
        test_assert(memcmp(out.info, ">Hello", 6) == 0, "info wrong");
    }
    test_end();

    test_begin("decode known binary frame with path");
    {
        /*
         * N0CALL-9>APRS,WIDE1-1*,WIDE2-1:!4903.50N/07201.75W-
         */
        uint8_t known[] = {
            /* dst: APRS */
            'A'<<1, 'P'<<1, 'R'<<1, 'S'<<1, ' '<<1, ' '<<1, 0xE0,
            /* src: N0CALL-9 */
            'N'<<1, '0'<<1, 'C'<<1, 'A'<<1, 'L'<<1, 'L'<<1, 0x72,
            /* path[0]: WIDE1-1 with H bit */
            'W'<<1, 'I'<<1, 'D'<<1, 'E'<<1, '1'<<1, ' '<<1, 0x80|0x60|(1<<1),
            /* path[1]: WIDE2-1 (last) */
            'W'<<1, 'I'<<1, 'D'<<1, 'E'<<1, '2'<<1, ' '<<1, 0x60|(1<<1)|0x01,
            /* control, PID */
            0x03, 0xF0,
            /* info */
            '!', '4', '9', '0', '3', '.', '5', '0', 'N', '/',
            '0', '7', '2', '0', '1', '.', '7', '5', 'W', '-'
        };
        ax25_ui_frame_t out;

        rc = ax25_decode_ui_frame(known, sizeof(known), &out);
        test_assert(rc == APRS_OK, "decode failed");
        test_assert(strcmp(out.dst.callsign, "APRS") == 0, "dst wrong");
        test_assert(strcmp(out.src.callsign, "N0CALL-9") == 0, "src wrong");
        test_assert(out.path_len == 2, "path_len wrong");
        test_assert(strcmp(out.path[0].callsign, "WIDE1-1*") == 0,
                    "path[0] wrong (should have *)");
        test_assert(strcmp(out.path[1].callsign, "WIDE2-1") == 0,
                    "path[1] wrong");
        test_assert(out.info_len == 20, "info_len wrong");
    }
    test_end();

    test_begin("encode then verify binary matches expected");
    {
        /* Encode N0CALL-9>APRS:>Hello and compare byte-for-byte */
        uint8_t expected[] = {
            'A'<<1, 'P'<<1, 'R'<<1, 'S'<<1, ' '<<1, ' '<<1, 0xE0,
            'N'<<1, '0'<<1, 'C'<<1, 'A'<<1, 'L'<<1, 'L'<<1, 0x60|(9<<1)|0x01,
            0x03, 0xF0,
            '>', 'H', 'e', 'l', 'l', 'o'
        };

        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL-9");
        frame.path_len = 0;
        memcpy(frame.info, ">Hello", 6);
        frame.info_len = 6;

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");
        test_assert(written == sizeof(expected), "size mismatch");
        test_assert(memcmp(buf, expected, sizeof(expected)) == 0,
                    "binary mismatch");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* KISS wrapping of AX.25 frame                                       */
    /* ------------------------------------------------------------------ */

    test_begin("AX.25 frame through KISS round-trip");
    {
        uint8_t ax_buf[AX25_FRAME_MAX];
        uint8_t kiss_buf[AX25_FRAME_MAX + 10];
        uint8_t kiss_payload[AX25_FRAME_MAX];
        size_t ax_len, kiss_len, kp_len;
        uint8_t port;
        kiss_cmd_t cmd;
        ax25_ui_frame_t out;
        const char *info = "!4903.50N/07201.75W-PHG2360";

        /* build frame */
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        snprintf(frame.path[0].callsign,
                 sizeof(frame.path[0].callsign), "WIDE1-1");
        frame.path_len = 1;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        /* AX.25 encode */
        rc = ax25_encode_ui_frame(&frame, ax_buf, sizeof(ax_buf), &ax_len);
        test_assert(rc == APRS_OK, "ax25 encode failed");

        /* KISS encode */
        rc = kiss_encode(0, KISS_CMD_DATA_FRAME, ax_buf, ax_len,
                         kiss_buf, sizeof(kiss_buf), &kiss_len);
        test_assert(rc == APRS_OK, "kiss encode failed");

        /* KISS decode */
        rc = kiss_decode(kiss_buf, kiss_len, &port, &cmd,
                         kiss_payload, sizeof(kiss_payload), &kp_len);
        test_assert(rc == APRS_OK, "kiss decode failed");
        test_assert(port == 0, "port wrong");
        test_assert(cmd == KISS_CMD_DATA_FRAME, "cmd wrong");
        test_assert(kp_len == ax_len, "payload len mismatch");

        /* AX.25 decode */
        rc = ax25_decode_ui_frame(kiss_payload, kp_len, &out);
        test_assert(rc == APRS_OK, "ax25 decode failed");
        test_assert(strcmp(out.dst.callsign, "APRS") == 0, "dst wrong");
        test_assert(strcmp(out.src.callsign, "N0CALL") == 0, "src wrong");
        test_assert(out.path_len == 1, "path_len wrong");
        test_assert(strcmp(out.path[0].callsign, "WIDE1-1") == 0,
                    "path[0] wrong");
        test_assert(out.info_len == strlen(info), "info_len wrong");
        test_assert(memcmp(out.info, info, strlen(info)) == 0, "info wrong");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* aprs_packet_t <-> ax25_ui_frame_t conversion                       */
    /* ------------------------------------------------------------------ */

    test_begin("ax25_from_aprs conversion");
    {
        aprs_packet_t pkt;
        ax25_ui_frame_t out;
        const char *path[] = {"WIDE1-1"};

        rc = aprs_build_status(&pkt, "N0CALL", "APRS",
                               path, 1, "On the air");
        test_assert(rc == APRS_OK, "build failed");

        rc = ax25_from_aprs(&pkt, &out);
        test_assert(rc == APRS_OK, "ax25_from_aprs failed");
        test_assert(strcmp(out.dst.callsign, "APRS") == 0, "dst wrong");
        test_assert(strcmp(out.src.callsign, "N0CALL") == 0, "src wrong");
        test_assert(out.path_len == 1, "path_len wrong");
        test_assert(strcmp(out.path[0].callsign, "WIDE1-1") == 0,
                    "path[0] wrong");
        test_assert(out.info_len == strlen(">On the air"), "info_len wrong");
        test_assert(memcmp(out.info, ">On the air", out.info_len) == 0,
                    "info wrong");
    }
    test_end();

    test_begin("ax25_to_aprs conversion");
    {
        aprs_packet_t pkt;
        const char *info = "!4903.50N/07201.75W-PHG2360";

        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL-9");
        snprintf(frame.path[0].callsign,
                 sizeof(frame.path[0].callsign), "WIDE1-1");
        frame.path_len = 1;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        rc = ax25_to_aprs(&frame, &pkt);
        test_assert(rc == APRS_OK, "ax25_to_aprs failed");
        test_assert(pkt.type == APRS_PACKET_POSITION, "wrong type");
        test_assert(strcmp(pkt.src.callsign, "N0CALL-9") == 0, "src wrong");
        test_assert(strcmp(pkt.dst.callsign, "APRS") == 0, "dst wrong");
        test_assert(pkt.path_len == 1, "path_len wrong");
        test_assert(pkt.data.position.latitude > 49.0 &&
                    pkt.data.position.latitude < 49.1,
                    "latitude wrong");
        test_assert(strcmp(pkt.data.position.comment, "PHG2360") == 0,
                    "comment wrong");
    }
    test_end();

    test_begin("APRS -> AX.25 binary -> APRS full round-trip");
    {
        aprs_packet_t pkt1, pkt2;
        ax25_ui_frame_t f1, f2;
        uint8_t binary[AX25_FRAME_MAX];
        size_t blen;
        const char *path[] = {"WIDE1-1", "WIDE2-1"};

        /* build APRS packet */
        rc = aprs_build_position(&pkt1, "W3ADO-1", "APDW15",
                                 path, 2,
                                 39.1525, -76.7275,
                                 '/', '#', "Baltimore");
        test_assert(rc == APRS_OK, "build failed");

        /* APRS -> ax25 struct */
        rc = ax25_from_aprs(&pkt1, &f1);
        test_assert(rc == APRS_OK, "ax25_from_aprs failed");

        /* ax25 struct -> binary */
        rc = ax25_encode_ui_frame(&f1, binary, sizeof(binary), &blen);
        test_assert(rc == APRS_OK, "encode failed");

        /* binary -> ax25 struct */
        rc = ax25_decode_ui_frame(binary, blen, &f2);
        test_assert(rc == APRS_OK, "decode failed");

        /* ax25 struct -> APRS */
        rc = ax25_to_aprs(&f2, &pkt2);
        test_assert(rc == APRS_OK, "ax25_to_aprs failed");

        /* verify */
        test_assert(pkt2.type == APRS_PACKET_POSITION, "wrong type");
        test_assert(strcmp(pkt2.src.callsign, "W3ADO-1") == 0, "src wrong");
        test_assert(strcmp(pkt2.dst.callsign, "APDW15") == 0, "dst wrong");
        test_assert(pkt2.path_len == 2, "path_len wrong");
        test_assert(pkt2.data.position.latitude > 39.1 &&
                    pkt2.data.position.latitude < 39.2,
                    "latitude wrong");
        test_assert(pkt2.data.position.longitude < -76.7 &&
                    pkt2.data.position.longitude > -76.8,
                    "longitude wrong");
        test_assert(strcmp(pkt2.data.position.comment, "Baltimore") == 0,
                    "comment wrong");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* FCS-16                                                              */
    /* ------------------------------------------------------------------ */

    test_begin("FCS-16 known vector: 123456789");
    {
        const uint8_t data[] = "123456789";
        uint16_t fcs = ax25_fcs16(data, 9);
        test_assert(fcs == 0x906E, "FCS wrong for 123456789");
    }
    test_end();

    test_begin("FCS-16 empty data");
    {
        uint16_t fcs = ax25_fcs16(NULL, 0);
        test_assert(fcs == 0x0000, "FCS of empty data wrong");
    }
    test_end();

    test_begin("FCS-16 on encoded frame is deterministic");
    {
        const char *info = ">test";
        uint16_t fcs1, fcs2;

        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        frame.path_len = 0;
        memcpy(frame.info, info, strlen(info));
        frame.info_len = strlen(info);

        rc = ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);
        test_assert(rc == APRS_OK, "encode failed");

        fcs1 = ax25_fcs16(buf, written);
        fcs2 = ax25_fcs16(buf, written);
        test_assert(fcs1 == fcs2, "FCS not deterministic");
        test_assert(fcs1 != 0, "FCS should be non-zero for real data");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* error cases                                                         */
    /* ------------------------------------------------------------------ */

    test_begin("encode rejects NULL frame");
    rc = ax25_encode_ui_frame(NULL, buf, sizeof(buf), &written);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL");
    test_end();

    test_begin("decode rejects too-short data");
    {
        uint8_t tiny[] = {0x00, 0x01, 0x02};
        ax25_ui_frame_t out;
        rc = ax25_decode_ui_frame(tiny, sizeof(tiny), &out);
        test_assert(rc == APRS_ERR_PARSE, "should reject < 16 bytes");
    }
    test_end();

    test_begin("decode rejects non-UI control byte");
    {
        /* Build a valid-looking frame but with control = 0x13 (not UI) */
        uint8_t bad[18];
        ax25_ui_frame_t out;
        memset(bad, ' ' << 1, 12); /* dummy addresses */
        bad[6] = 0x60;             /* dst SSID, no ext */
        bad[13] = 0x61;            /* src SSID, ext bit set (last) */
        bad[14] = 0x13;            /* not UI */
        bad[15] = 0xF0;
        bad[16] = '>';
        bad[17] = 'X';
        rc = ax25_decode_ui_frame(bad, sizeof(bad), &out);
        test_assert(rc == APRS_ERR_UNSUPPORTED, "should reject non-UI");
    }
    test_end();

    test_begin("ax25_from_aprs rejects NULL");
    rc = ax25_from_aprs(NULL, &frame);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL pkt");
    test_end();

    test_begin("ax25_to_aprs rejects NULL");
    {
        aprs_packet_t pkt;
        rc = ax25_to_aprs(NULL, &pkt);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL frame");
    }
    test_end();

    test_begin("encode buffer overflow");
    {
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        frame.path_len = 0;
        frame.info_len = 0;
        rc = ax25_encode_ui_frame(&frame, buf, 10, &written);
        test_assert(rc == APRS_ERR_OVERFLOW, "should overflow in 10 bytes");
    }
    test_end();
}
