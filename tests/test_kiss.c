/*
 * test_kiss.c — KISS single-frame encode/decode + streaming decoder tests
 */

#include <string.h>
#include "libaprs/kiss.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

/* ------------------------------------------------------------------ */
/* helpers for streaming decoder tests                                 */
/* ------------------------------------------------------------------ */

#define MAX_FRAMES 16

typedef struct {
    int count;
    uint8_t ports[MAX_FRAMES];
    kiss_cmd_t cmds[MAX_FRAMES];
    uint8_t payloads[MAX_FRAMES][256];
    size_t lens[MAX_FRAMES];
} frame_log_t;

static void log_frame(uint8_t port, kiss_cmd_t cmd,
                      const uint8_t *payload, size_t payload_len,
                      void *user)
{
    frame_log_t *log = (frame_log_t *)user;
    if (log->count >= MAX_FRAMES) return;
    log->ports[log->count] = port;
    log->cmds[log->count] = cmd;
    if (payload_len > 256) payload_len = 256;
    memcpy(log->payloads[log->count], payload, payload_len);
    log->lens[log->count] = payload_len;
    log->count++;
}

void test_kiss(void)
{
    uint8_t frame[256];
    uint8_t payload[256];
    size_t written, plen;
    uint8_t port;
    kiss_cmd_t cmd;
    aprs_err_t rc;

    /* ================================================================ */
    /* single-frame encode / decode (existing tests)                     */
    /* ================================================================ */

    test_begin("KISS encode simple data frame");
    {
        const uint8_t data[] = "Hello APRS";
        rc = kiss_encode(0, KISS_CMD_DATA_FRAME, data, 10,
                         frame, sizeof(frame), &written);
        test_assert(rc == APRS_OK, "encode failed");
        test_assert(frame[0] == KISS_FEND, "no leading FEND");
        test_assert(frame[1] == 0x00, "type byte wrong");
        test_assert(frame[written - 1] == KISS_FEND, "no trailing FEND");
    }
    test_end();

    test_begin("KISS decode simple data frame");
    rc = kiss_decode(frame, written, &port, &cmd,
                     payload, sizeof(payload), &plen);
    test_assert(rc == APRS_OK, "decode failed");
    test_assert(port == 0, "port wrong");
    test_assert(cmd == KISS_CMD_DATA_FRAME, "cmd wrong");
    test_assert(plen == 10, "payload len wrong");
    test_assert(memcmp(payload, "Hello APRS", 10) == 0, "payload wrong");
    test_end();

    test_begin("KISS encode with FEND in payload");
    {
        uint8_t data[] = {0x41, KISS_FEND, 0x42};
        rc = kiss_encode(0, KISS_CMD_DATA_FRAME, data, 3,
                         frame, sizeof(frame), &written);
        test_assert(rc == APRS_OK, "encode failed");
    }
    test_end();

    test_begin("KISS decode FEND escape");
    rc = kiss_decode(frame, written, &port, &cmd,
                     payload, sizeof(payload), &plen);
    test_assert(rc == APRS_OK, "decode failed");
    test_assert(plen == 3, "payload len wrong");
    test_assert(payload[1] == KISS_FEND, "FEND not restored");
    test_end();

    test_begin("KISS encode with FESC in payload");
    {
        uint8_t data[] = {KISS_FESC};
        rc = kiss_encode(0, KISS_CMD_DATA_FRAME, data, 1,
                         frame, sizeof(frame), &written);
        test_assert(rc == APRS_OK, "encode failed");
    }
    test_end();

    test_begin("KISS decode FESC escape");
    rc = kiss_decode(frame, written, &port, &cmd,
                     payload, sizeof(payload), &plen);
    test_assert(rc == APRS_OK, "decode failed");
    test_assert(plen == 1, "payload len wrong");
    test_assert(payload[0] == KISS_FESC, "FESC not restored");
    test_end();

    test_begin("KISS encode port 3 TXDELAY");
    {
        uint8_t data[] = {50};
        rc = kiss_encode(3, KISS_CMD_TXDELAY, data, 1,
                         frame, sizeof(frame), &written);
        test_assert(rc == APRS_OK, "encode failed");
        test_assert(frame[1] == 0x31, "type byte wrong");
    }
    test_end();

    test_begin("KISS decode port 3 TXDELAY");
    rc = kiss_decode(frame, written, &port, &cmd,
                     payload, sizeof(payload), &plen);
    test_assert(rc == APRS_OK, "decode failed");
    test_assert(port == 3, "port wrong");
    test_assert(cmd == KISS_CMD_TXDELAY, "cmd wrong");
    test_end();

    test_begin("KISS encode rejects port > 15");
    rc = kiss_encode(16, KISS_CMD_DATA_FRAME, payload, 1,
                     frame, sizeof(frame), &written);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject port 16");
    test_end();

    test_begin("KISS encode rejects tiny buffer");
    rc = kiss_encode(0, KISS_CMD_DATA_FRAME, payload, 1,
                     frame, 2, &written);
    test_assert(rc == APRS_ERR_OVERFLOW, "should overflow");
    test_end();

    test_begin("KISS decode rejects missing FEND");
    {
        uint8_t bad[] = {0x00, 0x00, 0x41, KISS_FEND};
        rc = kiss_decode(bad, 4, &port, &cmd,
                         payload, sizeof(payload), &plen);
        test_assert(rc == APRS_ERR_PARSE, "should reject");
    }
    test_end();

    test_begin("KISS decode rejects truncated escape");
    {
        uint8_t bad[] = {KISS_FEND, 0x00, KISS_FESC, KISS_FEND};
        rc = kiss_decode(bad, 4, &port, &cmd,
                         payload, sizeof(payload), &plen);
        test_assert(rc == APRS_ERR_PARSE, "should reject");
    }
    test_end();

    test_begin("KISS decode rejects NULL args");
    rc = kiss_decode(NULL, 10, &port, &cmd, payload, sizeof(payload), &plen);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL");
    test_end();

    test_begin("KISS encode empty payload");
    rc = kiss_encode(0, KISS_CMD_DATA_FRAME, NULL, 0,
                     frame, sizeof(frame), &written);
    test_assert(rc == APRS_OK, "encode failed");
    test_assert(written == 3, "should be FEND + type + FEND");
    test_end();

    test_begin("KISS decode empty payload");
    rc = kiss_decode(frame, written, &port, &cmd,
                     payload, sizeof(payload), &plen);
    test_assert(rc == APRS_OK, "decode failed");
    test_assert(plen == 0, "payload should be empty");
    test_end();

    /* ================================================================ */
    /* streaming decoder tests                                          */
    /* ================================================================ */

    test_begin("stream: single complete frame");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = "Test";
        uint8_t kiss[32];
        size_t klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 4,
                    kiss, sizeof(kiss), &klen);
        kiss_decoder_feed(&dec, kiss, klen);

        test_assert(log.count == 1, "should get 1 frame");
        test_assert(log.ports[0] == 0, "port wrong");
        test_assert(log.cmds[0] == KISS_CMD_DATA_FRAME, "cmd wrong");
        test_assert(log.lens[0] == 4, "payload len wrong");
        test_assert(memcmp(log.payloads[0], "Test", 4) == 0,
                    "payload wrong");
    }
    test_end();

    test_begin("stream: two frames in one chunk");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t d1[] = "AAA";
        uint8_t d2[] = "BBB";
        uint8_t combined[64];
        size_t k1, k2;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        kiss_encode(0, KISS_CMD_DATA_FRAME, d1, 3,
                    combined, sizeof(combined), &k1);
        kiss_encode(1, KISS_CMD_DATA_FRAME, d2, 3,
                    combined + k1, sizeof(combined) - k1, &k2);

        kiss_decoder_feed(&dec, combined, k1 + k2);

        test_assert(log.count == 2, "should get 2 frames");
        test_assert(log.ports[0] == 0, "frame 0 port wrong");
        test_assert(log.ports[1] == 1, "frame 1 port wrong");
        test_assert(memcmp(log.payloads[0], "AAA", 3) == 0,
                    "frame 0 payload wrong");
        test_assert(memcmp(log.payloads[1], "BBB", 3) == 0,
                    "frame 1 payload wrong");
    }
    test_end();

    test_begin("stream: frame split across two feeds");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = "SplitMe";
        uint8_t kiss[32];
        size_t klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 7,
                    kiss, sizeof(kiss), &klen);

        /* feed first half */
        kiss_decoder_feed(&dec, kiss, klen / 2);
        test_assert(log.count == 0, "no frame yet");

        /* feed second half */
        kiss_decoder_feed(&dec, kiss + klen / 2, klen - klen / 2);
        test_assert(log.count == 1, "should get 1 frame");
        test_assert(log.lens[0] == 7, "payload len wrong");
        test_assert(memcmp(log.payloads[0], "SplitMe", 7) == 0,
                    "payload wrong");
    }
    test_end();

    test_begin("stream: split in middle of escape sequence");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = {0x41, KISS_FEND, 0x42};
        uint8_t kiss[32];
        size_t klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 3,
                    kiss, sizeof(kiss), &klen);
        /* encoded: FEND 00 41 FESC TFEND 42 FEND
         * split between FESC and TFEND */

        /* find FESC position */
        {
            size_t split = 0, i;
            for (i = 2; i < klen - 1; i++) {
                if (kiss[i] == KISS_FESC) { split = i + 1; break; }
            }
            test_assert(split > 0, "FESC not found");

            kiss_decoder_feed(&dec, kiss, split);
            test_assert(log.count == 0, "no frame yet");

            kiss_decoder_feed(&dec, kiss + split, klen - split);
            test_assert(log.count == 1, "should get 1 frame");
            test_assert(log.lens[0] == 3, "payload len wrong");
            test_assert(log.payloads[0][1] == KISS_FEND,
                        "FEND not restored after split escape");
        }
    }
    test_end();

    test_begin("stream: garbage before first FEND ignored");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = "OK";
        uint8_t combined[32];
        uint8_t garbage[] = {0x55, 0xAA, 0x00, 0xFF};
        size_t klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        /* feed garbage first */
        kiss_decoder_feed(&dec, garbage, sizeof(garbage));
        test_assert(log.count == 0, "should ignore garbage");

        /* then a real frame */
        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 2,
                    combined, sizeof(combined), &klen);
        kiss_decoder_feed(&dec, combined, klen);
        test_assert(log.count == 1, "should get 1 frame");
        test_assert(memcmp(log.payloads[0], "OK", 2) == 0,
                    "payload wrong");
    }
    test_end();

    test_begin("stream: consecutive FENDs between frames");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = "X";
        uint8_t buf[32];
        size_t pos = 0, klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        /* build: FEND FEND FEND 00 'X' FEND FEND FEND */
        buf[pos++] = KISS_FEND;
        buf[pos++] = KISS_FEND;
        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 1,
                    buf + pos, sizeof(buf) - pos, &klen);
        pos += klen;
        buf[pos++] = KISS_FEND;

        kiss_decoder_feed(&dec, buf, pos);
        test_assert(log.count == 1, "should get exactly 1 frame");
    }
    test_end();

    test_begin("stream: byte-at-a-time feeding");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = "Byte";
        uint8_t kiss[32];
        size_t klen, i;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 4,
                    kiss, sizeof(kiss), &klen);

        for (i = 0; i < klen; i++)
            kiss_decoder_feed(&dec, &kiss[i], 1);

        test_assert(log.count == 1, "should get 1 frame");
        test_assert(log.lens[0] == 4, "payload len wrong");
        test_assert(memcmp(log.payloads[0], "Byte", 4) == 0,
                    "payload wrong");
    }
    test_end();

    test_begin("stream: reset clears state");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t half[] = {KISS_FEND, 0x00, 0x41, 0x42};
        uint8_t data[] = "After";
        uint8_t kiss[32];
        size_t klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        /* feed partial frame */
        kiss_decoder_feed(&dec, half, sizeof(half));
        test_assert(log.count == 0, "no frame yet");

        /* reset */
        kiss_decoder_reset(&dec);

        /* now feed a complete frame */
        kiss_encode(0, KISS_CMD_DATA_FRAME, data, 5,
                    kiss, sizeof(kiss), &klen);
        kiss_decoder_feed(&dec, kiss, klen);
        test_assert(log.count == 1, "should get 1 frame after reset");
        test_assert(memcmp(log.payloads[0], "After", 5) == 0,
                    "payload wrong");
    }
    test_end();

    test_begin("stream: TXDELAY command frame");
    {
        kiss_decoder_t dec;
        frame_log_t log;
        uint8_t data[] = {50};
        uint8_t kiss[16];
        size_t klen;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        kiss_encode(2, KISS_CMD_TXDELAY, data, 1,
                    kiss, sizeof(kiss), &klen);
        kiss_decoder_feed(&dec, kiss, klen);

        test_assert(log.count == 1, "should get 1 frame");
        test_assert(log.ports[0] == 2, "port wrong");
        test_assert(log.cmds[0] == KISS_CMD_TXDELAY, "cmd wrong");
        test_assert(log.lens[0] == 1, "payload len wrong");
        test_assert(log.payloads[0][0] == 50, "txdelay value wrong");
    }
    test_end();

    test_begin("stream: feed NULL with len 0 is OK");
    {
        kiss_decoder_t dec;
        frame_log_t log;

        memset(&log, 0, sizeof(log));
        kiss_decoder_init(&dec, log_frame, &log);

        rc = kiss_decoder_feed(&dec, NULL, 0);
        test_assert(rc == APRS_OK, "should accept NULL/0");
        test_assert(log.count == 0, "no frames");
    }
    test_end();

    test_begin("stream: feed rejects NULL with nonzero len");
    {
        kiss_decoder_t dec;
        kiss_decoder_init(&dec, NULL, NULL);
        rc = kiss_decoder_feed(&dec, NULL, 5);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject");
    }
    test_end();
}
