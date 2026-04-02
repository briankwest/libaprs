/*
 * aprs_sdr — Live APRS monitor using RTL-SDR
 *
 * Uses async IQ reading with a ring buffer to prevent sample loss
 * between reads (rtlsdr_read_sync drops IQ during FM processing,
 * causing phase discontinuities that break the AFSK PLL).
 *
 * Pipeline:
 *   RTL-SDR IQ @ 240 kHz (async reader thread → ring buffer)
 *   → FM discriminator → DC block → optional de-emphasis
 *   → decimate to 48 kHz → AFSK1200 demod → AX.25 → TNC2
 *
 * Usage:
 *   aprs_sdr                       # default: 144.390 MHz
 *   aprs_sdr -f 462.550 -g 400    # GMRS, 40 dB gain
 *   aprs_sdr -f 462.550 -g 400 -E # disable de-emphasis
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <rtl-sdr.h>

#include "libaprs/modem.h"
#include "libaprs/ax25.h"
#include "libaprs/aprs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEFAULT_SDR_RATE   240000
#define DEFAULT_AUDIO_RATE 48000
#define IQ_CHUNK           65536
#define APRS_FREQ_NA       144390000

/* ── ring buffer for async IQ reader ── */

#define RING_SIZE (IQ_CHUNK * 32)   /* ~4.5 seconds at 240 kHz */

static unsigned char  g_ring[RING_SIZE];
static volatile size_t g_ring_head = 0;
static volatile size_t g_ring_tail = 0;
static pthread_mutex_t g_ring_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_ring_cond = PTHREAD_COND_INITIALIZER;

static volatile int    g_running = 1;
static rtlsdr_dev_t  *g_dev;
static int             g_packet_count = 0;

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
    rtlsdr_cancel_async(g_dev);
}

/* async callback — runs on librtlsdr's internal thread */
static void rtlsdr_cb(unsigned char *buf, uint32_t len, void *ctx)
{
    (void)ctx;
    if (!g_running) { rtlsdr_cancel_async(g_dev); return; }

    pthread_mutex_lock(&g_ring_mtx);
    for (uint32_t i = 0; i < len; i++) {
        g_ring[g_ring_head % RING_SIZE] = buf[i];
        g_ring_head++;
    }
    pthread_cond_signal(&g_ring_cond);
    pthread_mutex_unlock(&g_ring_mtx);
}

static void *reader_thread(void *arg)
{
    (void)arg;
    rtlsdr_read_async(g_dev, rtlsdr_cb, NULL, 0, IQ_CHUNK);
    return NULL;
}

/* ── APRS packet callback ── */

static void on_frame(const uint8_t *frame, size_t frame_len, void *user)
{
    ax25_ui_frame_t ax;
    aprs_packet_t pkt;
    char line[1024];
    time_t now;
    struct tm tbuf, *t;

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

/* ── WAV helpers ── */

static FILE *wav_start(const char *path, int rate)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return NULL;
    uint32_t zero = 0;
    uint16_t fmt_tag = 1, channels = 1, bits = 16;
    uint32_t srate = (uint32_t)rate;
    uint32_t byte_rate = srate * 2;
    uint16_t block_align = 2;
    uint32_t fmt_size = 16;
    fwrite("RIFF", 1, 4, fp); fwrite(&zero, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp); fwrite(&fmt_size, 4, 1, fp);
    fwrite(&fmt_tag, 2, 1, fp); fwrite(&channels, 2, 1, fp);
    fwrite(&srate, 4, 1, fp); fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp); fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp); fwrite(&zero, 4, 1, fp);
    return fp;
}

static void wav_finish(FILE *fp)
{
    if (!fp) return;
    long pos = ftell(fp);
    uint32_t data_bytes = (uint32_t)(pos - 44);
    uint32_t riff_size = (uint32_t)(pos - 8);
    fseek(fp, 4, SEEK_SET);  fwrite(&riff_size, 4, 1, fp);
    fseek(fp, 40, SEEK_SET); fwrite(&data_bytes, 4, 1, fp);
    fclose(fp);
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-f freq] [-d dev] [-g gain] [-r rate] [-s sdr_rate] [-E] [-i] [-w file]\n", prog);
    fprintf(stderr, "  -f  frequency in MHz or Hz (default 144.390)\n");
    fprintf(stderr, "  -d  RTL-SDR device index (default 0)\n");
    fprintf(stderr, "  -g  tuner gain in dB*10 (default auto)\n");
    fprintf(stderr, "  -r  audio output rate (default 48000)\n");
    fprintf(stderr, "  -s  SDR sample rate (default 240000)\n");
    fprintf(stderr, "  -E  disable de-emphasis\n");
    fprintf(stderr, "  -i  invert polarity\n");
    fprintf(stderr, "  -w  dump audio to WAV file\n");
}

int main(int argc, char **argv)
{
    uint32_t freq = APRS_FREQ_NA;
    int device_index = 0;
    int gain = -1;
    int audio_rate = DEFAULT_AUDIO_RATE;
    uint32_t sdr_rate = DEFAULT_SDR_RATE;
    const char *wav_path = NULL;
    int no_deemph = 0;
    int invert = 0;
    int i, rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            double f = atof(argv[++i]);
            if (f < 10000) f *= 1e6;
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
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
            wav_path = argv[++i];
        else if (strcmp(argv[i], "-E") == 0)
            no_deemph = 1;
        else if (strcmp(argv[i], "-i") == 0)
            invert = 1;
        else { usage(argv[0]); return 1; }
    }

    /* open SDR */
    {
        int count = (int)rtlsdr_get_device_count();
        if (count == 0) {
            fprintf(stderr, "No RTL-SDR devices found\n");
            return 1;
        }
        fprintf(stderr, "Found %d RTL-SDR device(s)\n", count);
    }

    rc = rtlsdr_open(&g_dev, (uint32_t)device_index);
    if (rc < 0) {
        fprintf(stderr, "Failed to open device %d (rc=%d)\n", device_index, rc);
        return 1;
    }

    rtlsdr_set_center_freq(g_dev, freq);
    rtlsdr_set_sample_rate(g_dev, sdr_rate);

    if (gain < 0) {
        rtlsdr_set_tuner_gain_mode(g_dev, 0);
        fprintf(stderr, "Gain: auto\n");
    } else {
        rtlsdr_set_tuner_gain_mode(g_dev, 1);
        rtlsdr_set_tuner_gain(g_dev, gain);
        fprintf(stderr, "Gain: %.1f dB\n", gain / 10.0);
    }

    rtlsdr_reset_buffer(g_dev);

    fprintf(stderr, "%.4f MHz | SDR %u Hz | Audio %d Hz%s%s\n",
            freq / 1e6, sdr_rate, audio_rate,
            no_deemph ? " | no de-emph" : "",
            invert ? " | inverted" : "");
    fprintf(stderr, "Listening... (Ctrl-C to stop)\n\n");

    /* create demod */
    afsk_demod_t *demod = afsk_demod_create(audio_rate, on_frame, NULL);
    if (!demod) {
        fprintf(stderr, "Failed to create demodulator\n");
        rtlsdr_close(g_dev);
        return 1;
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    FILE *wav_fp = wav_path ? wav_start(wav_path, audio_rate) : NULL;

    /* start async IQ reader */
    pthread_t reader;
    pthread_create(&reader, NULL, reader_thread, NULL);

    /* FM demod state */
    float prev_i = 0, prev_q = 0;
    float dc_x_prev = 0, dc_y_prev = 0;
    const float alpha_dc = 0.998f;
    float deemph_val = 0;
    float alpha_de = 1.0f / (1.0f + (float)sdr_rate * 75e-6f);
    double dec_step = (double)audio_rate / (double)sdr_rate;
    double dec_acc = 0.0;
    float dec_sum = 0;
    int dec_count = 0;
    float scale = 16000.0f / ((float)(2.0 * M_PI * 5000.0) / (float)sdr_rate);

    int16_t audio_buf[16384];
    unsigned char iq_chunk[IQ_CHUNK];

    /* squelch state (for logging only — demod always fed) */
    #define SQ_OPEN_THRESH   15000
    #define SQ_CLOSE_THRESH  17000
    #define SQ_DEBOUNCE      5
    int sq_open = 0, sq_open_cnt = 0, sq_close_cnt = 0;
    time_t sq_open_time = 0;
    int prev_pkt_count = 0;

    /* main processing loop */
    while (g_running) {
        /* wait for IQ data from ring buffer */
        pthread_mutex_lock(&g_ring_mtx);
        while (g_ring_head - g_ring_tail < IQ_CHUNK && g_running)
            pthread_cond_wait(&g_ring_cond, &g_ring_mtx);
        if (!g_running) { pthread_mutex_unlock(&g_ring_mtx); break; }

        for (i = 0; i < IQ_CHUNK; i++)
            iq_chunk[i] = g_ring[(g_ring_tail + i) % RING_SIZE];
        g_ring_tail += IQ_CHUNK;
        pthread_mutex_unlock(&g_ring_mtx);

        /* IQ → FM demod → DC block → de-emphasis → decimate */
        int nsamples = IQ_CHUNK / 2;
        int audio_pos = 0;

        for (i = 0; i < nsamples; i++) {
            float si = ((float)iq_chunk[i * 2]     - 127.5f) / 127.5f;
            float sq = ((float)iq_chunk[i * 2 + 1] - 127.5f) / 127.5f;

            float dot   = si * prev_i + sq * prev_q;
            float cross = sq * prev_i - si * prev_q;
            float fm    = atan2f(cross, dot) * (invert ? -1.0f : 1.0f);
            prev_i = si;
            prev_q = sq;

            float dc_out = fm - dc_x_prev + alpha_dc * dc_y_prev;
            dc_x_prev = fm;
            dc_y_prev = dc_out;

            if (!no_deemph)
                deemph_val += alpha_de * (dc_out - deemph_val);

            dec_sum += no_deemph ? dc_out : deemph_val;
            dec_count++;
            dec_acc += dec_step;

            if (dec_acc >= 1.0) {
                dec_acc -= 1.0;
                float avg = (dec_sum / dec_count) * scale;
                if (avg > 32767.0f) avg = 32767.0f;
                if (avg < -32768.0f) avg = -32768.0f;
                audio_buf[audio_pos++] = (int16_t)avg;
                dec_sum = 0;
                dec_count = 0;
            }
        }

        if (audio_pos == 0) continue;

        /* WAV dump (all audio) */
        if (wav_fp)
            fwrite(audio_buf, 2, (size_t)audio_pos, wav_fp);

        /* feed AFSK demod (always — no squelch gating) */
        afsk_demod_feed(demod, audio_buf, (size_t)audio_pos);

        /* squelch for logging only */
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

            if (rms <= SQ_OPEN_THRESH) { sq_open_cnt++; sq_close_cnt = 0; }
            else if (rms > SQ_CLOSE_THRESH) { sq_close_cnt++; sq_open_cnt = 0; }
            else { sq_open_cnt = 0; sq_close_cnt = 0; }

            if (!sq_open && sq_open_cnt >= SQ_DEBOUNCE) {
                sq_open = 1;
                sq_open_time = time(NULL);
                prev_pkt_count = g_packet_count;
                time_t now = time(NULL);
                struct tm tbuf, *t = localtime_r(&now, &tbuf);
                fprintf(stderr, "[%02d:%02d:%02d] >>> %.4f MHz active (RMS %d)\n",
                        t->tm_hour, t->tm_min, t->tm_sec, freq / 1e6, rms);
            }

            if (sq_open && sq_close_cnt >= SQ_DEBOUNCE) {
                double dur = difftime(time(NULL), sq_open_time);
                if (dur < 0.1) dur = 0.1;
                int decoded = g_packet_count - prev_pkt_count;
                time_t now = time(NULL);
                struct tm tbuf, *t = localtime_r(&now, &tbuf);
                fprintf(stderr, "[%02d:%02d:%02d] <<< %.4f MHz idle  (%.1fs, decoded %d packet%s)\n",
                        t->tm_hour, t->tm_min, t->tm_sec, freq / 1e6, dur,
                        decoded, decoded == 1 ? "" : "s");
                sq_open = 0;
                sq_open_cnt = 0;
            }
        }
    }

    /* cleanup */
    rtlsdr_cancel_async(g_dev);
    pthread_join(reader, NULL);

    if (wav_fp) {
        wav_finish(wav_fp);
        fprintf(stderr, "Audio written to %s\n", wav_path);
    }

    fprintf(stderr, "\n%d packets decoded\n", g_packet_count);
    afsk_demod_destroy(demod);
    rtlsdr_close(g_dev);
    return 0;
}
