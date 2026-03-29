/*
 * test_modem.c — AFSK1200 modulator/demodulator tests
 *
 * Core test: modulate a known AX.25 frame, then demodulate the
 * resulting audio and verify the decoded frame matches byte-for-byte.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libaprs/modem.h"
#include "libaprs/ax25.h"
#include "libaprs/aprs.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

/* ------------------------------------------------------------------ */
/* callback state for demodulator tests                                */
/* ------------------------------------------------------------------ */

#define MAX_RX_FRAMES 8

typedef struct {
    int count;
    uint8_t frames[MAX_RX_FRAMES][HDLC_MAX_FRAME];
    size_t lens[MAX_RX_FRAMES];
} rx_log_t;

static void rx_callback(const uint8_t *frame, size_t frame_len, void *user)
{
    rx_log_t *log = (rx_log_t *)user;
    if (log->count >= MAX_RX_FRAMES) return;
    if (frame_len > HDLC_MAX_FRAME) frame_len = HDLC_MAX_FRAME;
    memcpy(log->frames[log->count], frame, frame_len);
    log->lens[log->count] = frame_len;
    log->count++;
}

void test_modem(void)
{
    /* ------------------------------------------------------------------ */
    /* modulator create/destroy                                            */
    /* ------------------------------------------------------------------ */

    test_begin("modulator create and destroy");
    {
        afsk_mod_t *m = afsk_mod_create(8000);
        test_assert(m != NULL, "create returned NULL");
        afsk_mod_destroy(m);
    }
    test_end();

    test_begin("modulator rejects bad sample rate");
    {
        afsk_mod_t *m = afsk_mod_create(0);
        test_assert(m == NULL, "should reject 0");
        m = afsk_mod_create(-1);
        test_assert(m == NULL, "should reject negative");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* demodulator create/destroy                                          */
    /* ------------------------------------------------------------------ */

    test_begin("demodulator create and destroy");
    {
        afsk_demod_t *d = afsk_demod_create(8000, NULL, NULL);
        test_assert(d != NULL, "create returned NULL");
        afsk_demod_destroy(d);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* modulate a frame — verify it produces samples                      */
    /* ------------------------------------------------------------------ */

    test_begin("modulate produces samples");
    {
        afsk_mod_t *m = afsk_mod_create(8000);
        ax25_ui_frame_t frame;
        uint8_t ax_buf[256];
        size_t ax_len;
        int16_t *audio;
        size_t nsamples;
        aprs_err_t rc;

        /* build a simple AX.25 frame */
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        frame.path_len = 0;
        memcpy(frame.info, ">test", 5);
        frame.info_len = 5;

        rc = ax25_encode_ui_frame(&frame, ax_buf, sizeof(ax_buf), &ax_len);
        test_assert(rc == APRS_OK, "ax25 encode failed");

        audio = (int16_t *)malloc(200000 * sizeof(int16_t));
        test_assert(audio != NULL, "malloc failed");

        rc = afsk_mod_frame(m, ax_buf, ax_len, audio, 200000, &nsamples);
        test_assert(rc == APRS_OK, "modulate failed");
        test_assert(nsamples > 0, "should produce samples");
        /* at 8000 Hz, ~6.7 samples/bit, a minimal frame should
         * produce at least a few hundred samples */
        test_assert(nsamples > 500, "too few samples");

        free(audio);
        afsk_mod_destroy(m);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* modulate then demodulate — the core round-trip test                 */
    /* ------------------------------------------------------------------ */

    test_begin("modulate -> demodulate round-trip");
    {
        afsk_mod_t *mod;
        afsk_demod_t *demod;
        ax25_ui_frame_t frame;
        uint8_t ax_buf[256];
        size_t ax_len;
        int16_t *audio;
        size_t nsamples;
        rx_log_t log;
        aprs_err_t rc;
        int rate = 16000;

        memset(&log, 0, sizeof(log));

        mod = afsk_mod_create(rate);
        demod = afsk_demod_create(rate, rx_callback, &log);
        test_assert(mod != NULL && demod != NULL, "create failed");

        /* build AX.25 frame: N0CALL>APRS:>Hello Modem */
        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL");
        frame.path_len = 0;
        memcpy(frame.info, ">Hello Modem", 12);
        frame.info_len = 12;

        rc = ax25_encode_ui_frame(&frame, ax_buf, sizeof(ax_buf), &ax_len);
        test_assert(rc == APRS_OK, "encode failed");

        /* modulate */
        audio = (int16_t *)malloc(500000 * sizeof(int16_t));
        test_assert(audio != NULL, "malloc failed");

        rc = afsk_mod_frame(mod, ax_buf, ax_len, audio, 500000, &nsamples);
        test_assert(rc == APRS_OK, "modulate failed");

        /* demodulate */
        rc = afsk_demod_feed(demod, audio, nsamples);
        test_assert(rc == APRS_OK, "demod feed failed");

        /* check result */
        test_assert(log.count == 1, "should decode 1 frame");
        if (log.count >= 1) {
            test_assert(log.lens[0] == ax_len,
                        "decoded frame length wrong");
            test_assert(memcmp(log.frames[0], ax_buf, ax_len) == 0,
                        "decoded frame content wrong");
        }

        free(audio);
        afsk_demod_destroy(demod);
        afsk_mod_destroy(mod);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* round-trip with path and longer info                                */
    /* ------------------------------------------------------------------ */

    test_begin("round-trip with path");
    {
        afsk_mod_t *mod;
        afsk_demod_t *demod;
        ax25_ui_frame_t frame;
        uint8_t ax_buf[512];
        size_t ax_len;
        int16_t *audio;
        size_t nsamples;
        rx_log_t log;
        int rate = 16000;
        const char *info = "!4903.50N/07201.75W-PHG2360";

        memset(&log, 0, sizeof(log));

        mod = afsk_mod_create(rate);
        demod = afsk_demod_create(rate, rx_callback, &log);

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

        ax25_encode_ui_frame(&frame, ax_buf, sizeof(ax_buf), &ax_len);

        audio = (int16_t *)malloc(500000 * sizeof(int16_t));
        afsk_mod_frame(mod, ax_buf, ax_len, audio, 500000, &nsamples);
        afsk_demod_feed(demod, audio, nsamples);

        test_assert(log.count == 1, "should decode 1 frame");
        if (log.count >= 1) {
            test_assert(log.lens[0] == ax_len, "length wrong");
            test_assert(memcmp(log.frames[0], ax_buf, ax_len) == 0,
                        "content wrong");
        }

        free(audio);
        afsk_demod_destroy(demod);
        afsk_mod_destroy(mod);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* full stack: APRS build -> AX.25 -> modulate -> demodulate -> parse  */
    /* ------------------------------------------------------------------ */

    test_begin("full stack: build -> modulate -> demod -> parse");
    {
        afsk_mod_t *mod;
        afsk_demod_t *demod;
        aprs_packet_t pkt_out, pkt_in;
        ax25_ui_frame_t f_out, f_in;
        uint8_t ax_buf[512];
        size_t ax_len;
        int16_t *audio;
        size_t nsamples;
        rx_log_t log;
        int rate = 32000;
        const char *path[] = {"WIDE1-1"};

        memset(&log, 0, sizeof(log));

        /* build APRS packet */
        aprs_build_position(&pkt_out, "N0CALL", "APRS", path, 1,
                            39.1525, -76.7275, '/', '>', "Baltimore");

        /* APRS -> AX.25 */
        ax25_from_aprs(&pkt_out, &f_out);
        ax25_encode_ui_frame(&f_out, ax_buf, sizeof(ax_buf), &ax_len);

        /* modulate */
        mod = afsk_mod_create(rate);
        audio = (int16_t *)malloc(500000 * sizeof(int16_t));
        afsk_mod_frame(mod, ax_buf, ax_len, audio, 500000, &nsamples);

        /* demodulate */
        demod = afsk_demod_create(rate, rx_callback, &log);
        afsk_demod_feed(demod, audio, nsamples);

        test_assert(log.count == 1, "should decode 1 frame");

        if (log.count >= 1) {
            /* AX.25 -> APRS */
            ax25_decode_ui_frame(log.frames[0], log.lens[0], &f_in);
            ax25_to_aprs(&f_in, &pkt_in);

            test_assert(pkt_in.type == APRS_PACKET_POSITION,
                        "wrong type");
            test_assert(strcmp(pkt_in.src.callsign, "N0CALL") == 0,
                        "src wrong");
            test_assert(pkt_in.data.position.latitude > 39.1 &&
                        pkt_in.data.position.latitude < 39.2,
                        "latitude wrong");
            test_assert(strcmp(pkt_in.data.position.comment,
                              "Baltimore") == 0,
                        "comment wrong");
        }

        free(audio);
        afsk_demod_destroy(demod);
        afsk_mod_destroy(mod);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* at 48000 Hz sample rate                                             */
    /* ------------------------------------------------------------------ */

    test_begin("round-trip at 48000 Hz");
    {
        afsk_mod_t *mod;
        afsk_demod_t *demod;
        ax25_ui_frame_t frame;
        uint8_t ax_buf[256];
        size_t ax_len;
        int16_t *audio;
        size_t nsamples;
        rx_log_t log;
        int rate = 48000;

        memset(&log, 0, sizeof(log));

        mod = afsk_mod_create(rate);
        demod = afsk_demod_create(rate, rx_callback, &log);

        ax25_ui_frame_init(&frame);
        snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
        snprintf(frame.src.callsign, sizeof(frame.src.callsign), "TEST");
        frame.path_len = 0;
        memcpy(frame.info, ">48k test", 9);
        frame.info_len = 9;

        ax25_encode_ui_frame(&frame, ax_buf, sizeof(ax_buf), &ax_len);

        audio = (int16_t *)malloc(1000000 * sizeof(int16_t));
        afsk_mod_frame(mod, ax_buf, ax_len, audio, 1000000, &nsamples);
        afsk_demod_feed(demod, audio, nsamples);

        test_assert(log.count == 1, "should decode 1 frame");
        if (log.count >= 1) {
            test_assert(log.lens[0] == ax_len, "length wrong");
            test_assert(memcmp(log.frames[0], ax_buf, ax_len) == 0,
                        "content wrong");
        }

        free(audio);
        afsk_demod_destroy(demod);
        afsk_mod_destroy(mod);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* error cases                                                         */
    /* ------------------------------------------------------------------ */

    test_begin("mod_frame rejects NULL");
    {
        aprs_err_t rc;
        int16_t buf[10];
        size_t n;
        rc = afsk_mod_frame(NULL, (uint8_t*)"x", 1, buf, 10, &n);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL mod");
    }
    test_end();

    test_begin("demod_feed rejects NULL");
    {
        aprs_err_t rc = afsk_demod_feed(NULL, NULL, 0);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL demod");
    }
    test_end();
}
