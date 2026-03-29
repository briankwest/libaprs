/*
 * aprs_decode — decode APRS packets from a WAV file
 *
 * Usage: aprs_decode <file.wav>
 *
 * Reads a PCM WAV file, demodulates AFSK1200, decodes AX.25 frames,
 * and prints TNC2 lines to stdout.  Reports packet count on stderr.
 * Comparable to Dire Wolf's atest.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libaprs/wav.h"
#include "libaprs/modem.h"
#include "libaprs/ax25.h"
#include "libaprs/aprs.h"

static int g_packet_count;

static void on_frame(const uint8_t *frame, size_t frame_len, void *user)
{
    ax25_ui_frame_t ax;
    aprs_packet_t pkt;
    char line[1024];

    (void)user;

    if (ax25_decode_ui_frame(frame, frame_len, &ax) != APRS_OK)
        return;

    if (ax25_to_aprs(&ax, &pkt) != APRS_OK)
        return;

    if (aprs_format_tnc2(&pkt, line, sizeof(line)) != APRS_OK)
        return;

    g_packet_count++;
    printf("%4d: %s\n", g_packet_count, line);
}

int main(int argc, char **argv)
{
    wav_reader_t wav;
    afsk_demod_t *demod;
    int16_t buf[4096];
    size_t nread;
    struct timespec t_start, t_end;
    double elapsed, audio_sec;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.wav>\n", argv[0]);
        return 1;
    }

    if (wav_open(&wav, argv[1]) != APRS_OK) {
        fprintf(stderr, "Failed to open %s\n", argv[1]);
        return 1;
    }

    fprintf(stderr, "WAV: %d Hz, %d-bit, %d ch, %u bytes PCM\n",
            wav.sample_rate, wav.bits_per_sample, wav.channels,
            wav.data_bytes);

    demod = afsk_demod_create(wav.sample_rate, on_frame, NULL);
    if (!demod) {
        fprintf(stderr, "Failed to create demodulator\n");
        wav_close(&wav);
        return 1;
    }

    g_packet_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    while (1) {
        if (wav_read(&wav, buf, sizeof(buf) / sizeof(buf[0]), &nread) != APRS_OK)
            break;
        if (nread == 0)
            break;
        afsk_demod_feed(demod, buf, nread);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    elapsed = (double)(t_end.tv_sec - t_start.tv_sec)
            + (double)(t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    audio_sec = (double)wav.data_bytes
              / ((double)wav.sample_rate * wav.channels * (wav.bits_per_sample / 8));

    fprintf(stderr, "\n%d packets decoded in %.3f seconds.  %.1f x realtime\n",
            g_packet_count, elapsed,
            elapsed > 0.0 ? audio_sec / elapsed : 0.0);

    afsk_demod_destroy(demod);
    wav_close(&wav);

    return 0;
}
