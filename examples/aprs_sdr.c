/*
 * aprs_sdr — Live APRS monitor using RTL-SDR
 *
 * Tunes an RTL-SDR dongle to 144.390 MHz (North America APRS),
 * FM demodulates the IQ stream, feeds audio into the AFSK1200
 * demodulator, and prints decoded TNC2 packets to stdout.
 *
 * Usage:
 *   aprs_sdr                       # default: 144.390 MHz, device 0
 *   aprs_sdr -f 144.800            # Europe: 144.800 MHz
 *   aprs_sdr -d 1                  # use second RTL-SDR device
 *   aprs_sdr -g 40                 # set tuner gain (dB * 10)
 *
 * Based on the proven SDR pipeline from kerchunk/mod_sdr.c:
 *   RTL-SDR IQ @ 240 kHz → FM demod → de-emphasis → decimate
 *   → 48000 Hz audio → AFSK1200 demod → AX.25 decode → TNC2 print
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <time.h>
#include <rtl-sdr.h>

#include "libaprs/modem.h"
#include "libaprs/ax25.h"
#include "libaprs/aprs.h"

/* SDR parameters */
#define DEFAULT_SDR_RATE  240000
#define DEFAULT_AUDIO_RATE 48000
#define IQ_BUFSZ          (16384 * 4)
#define APRS_FREQ_NA      144390000   /* 144.390 MHz */

/* decimation is not integer — use fractional accumulator */

static volatile int g_running = 1;
static int g_packet_count = 0;

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* callback when a complete AX.25 frame is decoded from audio */
static void on_frame(const uint8_t *frame, size_t frame_len, void *user)
{
    ax25_ui_frame_t ax;
    aprs_packet_t pkt;
    char line[1024];
    time_t now;
    struct tm tbuf;
    struct tm *t;

    (void)user;

    if (ax25_decode_ui_frame(frame, frame_len, &ax) != APRS_OK)
        return;
    if (ax25_to_aprs(&ax, &pkt) != APRS_OK)
        return;
    if (aprs_format_tnc2(&pkt, line, sizeof(line)) != APRS_OK)
        return;

    g_packet_count++;

    now = time(NULL);
    t = localtime_r(&now, &tbuf);

    printf("[%02d:%02d:%02d] %4d  %s\n",
           t->tm_hour, t->tm_min, t->tm_sec,
           g_packet_count, line);
    fflush(stdout);
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-f freq_hz] [-d device_index] [-g gain] [-r audio_rate] [-s sdr_rate]\n", prog);
    fprintf(stderr, "  -f  frequency in MHz or Hz (default 144.390)\n");
    fprintf(stderr, "  -d  RTL-SDR device index (default 0)\n");
    fprintf(stderr, "  -g  tuner gain in dB*10 (default auto)\n");
    fprintf(stderr, "  -r  audio output rate in Hz (default 48000)\n");
    fprintf(stderr, "  -s  SDR sample rate in Hz (default 240000)\n");
}

int main(int argc, char **argv)
{
    rtlsdr_dev_t *dev = NULL;
    afsk_demod_t *demod = NULL;
    uint32_t freq = APRS_FREQ_NA;
    int device_index = 0;
    int gain = -1; /* -1 = auto */
    int audio_rate = DEFAULT_AUDIO_RATE;
    uint32_t sdr_rate = DEFAULT_SDR_RATE;
    int rc, i;

    /* parse args */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            double f = atof(argv[++i]);
            if (f < 10000) f *= 1e6;  /* treat as MHz if < 10 kHz */
            freq = (uint32_t)f;
        }
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            device_index = atoi(argv[++i]);
        else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc)
            gain = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            audio_rate = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            sdr_rate = (uint32_t)atol(argv[++i]);
        else {
            usage(argv[0]);
            return 1;
        }
    }

    /* check for devices */
    {
        int count = (int)rtlsdr_get_device_count();
        if (count == 0) {
            fprintf(stderr, "No RTL-SDR devices found\n");
            return 1;
        }
        fprintf(stderr, "Found %d RTL-SDR device(s)\n", count);
        for (i = 0; i < count; i++)
            fprintf(stderr, "  [%d] %s\n", i, rtlsdr_get_device_name((uint32_t)i));
    }

    /* open device */
    rc = rtlsdr_open(&dev, (uint32_t)device_index);
    if (rc < 0) {
        fprintf(stderr, "Failed to open device %d (rc=%d)\n", device_index, rc);
        return 1;
    }

    /* configure */
    rtlsdr_set_center_freq(dev, freq);
    rtlsdr_set_sample_rate(dev, sdr_rate);

    if (gain < 0) {
        rtlsdr_set_tuner_gain_mode(dev, 0); /* auto */
        fprintf(stderr, "Gain: auto\n");
    } else {
        rtlsdr_set_tuner_gain_mode(dev, 1); /* manual */
        rtlsdr_set_tuner_gain(dev, gain);
        fprintf(stderr, "Gain: %.1f dB\n", gain / 10.0);
    }

    rtlsdr_reset_buffer(dev);

    fprintf(stderr, "Tuned to %.4f MHz @ %u Hz IQ, audio %d Hz\n",
            freq / 1e6, sdr_rate, audio_rate);
    fprintf(stderr, "Listening for APRS packets... (Ctrl-C to stop)\n\n");

    /* create AFSK1200 demodulator at audio rate */
    demod = afsk_demod_create(audio_rate, on_frame, NULL);
    if (!demod) {
        fprintf(stderr, "Failed to create demodulator\n");
        rtlsdr_close(dev);
        return 1;
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* main loop */
    {
        unsigned char *iq_buf = (unsigned char *)malloc(IQ_BUFSZ);
        int16_t *audio_buf = (int16_t *)malloc(IQ_BUFSZ * sizeof(int16_t));

        float prev_i = 0, prev_q = 0;
        float deemph = 0;

        /* de-emphasis: 75 µs time constant (first-order IIR, forward Euler) */
        float alpha = 1.0f / (1.0f + (float)sdr_rate * 75e-6f);

        /* fractional decimation: sdr_rate → audio_rate */
        double dec_step = (double)audio_rate / (double)sdr_rate;
        double dec_acc = 0.0;

        /* squelch / activity detection */
        #define SQ_OPEN_THRESH   2000
        #define SQ_CLOSE_THRESH  3000
        #define SQ_DEBOUNCE      3
        int sq_open = 0;
        int sq_open_cnt = 0, sq_close_cnt = 0;
        time_t sq_open_time = 0;
        int32_t peak_rms = 0;
        int prev_pkt_count = 0;

        while (g_running) {
            int n_read = 0;

            rc = rtlsdr_read_sync(dev, iq_buf, IQ_BUFSZ, &n_read);
            if (rc < 0) {
                fprintf(stderr, "rtlsdr_read_sync failed (rc=%d)\n", rc);
                break;
            }
            if (n_read == 0) continue;

            int nsamples = n_read / 2;
            int audio_pos = 0;

            /* IQ → FM demod → de-emphasis → decimate */
            for (i = 0; i < nsamples; i++) {
                float si = ((float)iq_buf[i * 2]     - 127.5f) / 127.5f;
                float sq = ((float)iq_buf[i * 2 + 1] - 127.5f) / 127.5f;

                float dot   = si * prev_i + sq * prev_q;
                float cross = sq * prev_i - si * prev_q;
                float fm = atan2f(cross, dot);
                prev_i = si;
                prev_q = sq;

                float audio = fm * (16000.0f / 0.07f);
                deemph = deemph + alpha * (audio - deemph);

                dec_acc += dec_step;
                if (dec_acc >= 1.0) {
                    dec_acc -= 1.0;
                    audio_buf[audio_pos++] = (int16_t)deemph;
                }
            }

            if (audio_pos == 0) continue;

            /* compute RMS of this audio chunk */
            {
                int64_t sum = 0;
                int j;
                for (j = 0; j < audio_pos; j++) {
                    int32_t v = audio_buf[j];
                    sum += v * v;
                }
                int32_t rms = 0;
                int64_t avg = sum / audio_pos;
                while ((int64_t)rms * rms < avg) rms++;

                /* squelch with debounce (FM-inverted: low RMS = signal) */
                if (rms <= SQ_OPEN_THRESH) {
                    sq_open_cnt++;
                    sq_close_cnt = 0;
                } else if (rms > SQ_CLOSE_THRESH) {
                    sq_close_cnt++;
                    sq_open_cnt = 0;
                } else {
                    sq_open_cnt = 0;
                    sq_close_cnt = 0;
                }

                if (!sq_open && sq_open_cnt >= SQ_DEBOUNCE) {
                    sq_open = 1;
                    sq_open_time = time(NULL);
                    peak_rms = rms;
                    prev_pkt_count = g_packet_count;

                    time_t now = time(NULL);
                    struct tm tbuf;
                    struct tm *t = localtime_r(&now, &tbuf);
                    fprintf(stderr, "[%02d:%02d:%02d] >>> signal detected (RMS %d)\n",
                            t->tm_hour, t->tm_min, t->tm_sec, rms);
                }

                if (sq_open) {
                    if (rms > peak_rms) peak_rms = rms;
                }

                if (sq_open && sq_close_cnt >= SQ_DEBOUNCE) {
                    double dur = difftime(time(NULL), sq_open_time);
                    if (dur < 0.1) dur = 0.1;
                    int decoded = g_packet_count - prev_pkt_count;

                    time_t now = time(NULL);
                    struct tm tbuf;
                    struct tm *t = localtime_r(&now, &tbuf);
                    fprintf(stderr,
                            "[%02d:%02d:%02d] <<< signal lost  (%.1fs, peak RMS %d, decoded %d packet%s)\n",
                            t->tm_hour, t->tm_min, t->tm_sec,
                            dur, peak_rms,
                            decoded, decoded == 1 ? "" : "s");

                    sq_open = 0;
                    sq_open_cnt = 0;
                }
            }

            /* feed audio to AFSK demodulator */
            afsk_demod_feed(demod, audio_buf, (size_t)audio_pos);
        }

        free(iq_buf);
        free(audio_buf);
    }

    fprintf(stderr, "\n%d packets decoded\n", g_packet_count);

    afsk_demod_destroy(demod);
    rtlsdr_close(dev);

    return 0;
}
