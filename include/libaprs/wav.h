#ifndef LIBAPRS_WAV_H
#define LIBAPRS_WAV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FILE *fp;
    int sample_rate;
    int bits_per_sample;
    int channels;
    uint32_t data_bytes;     /* total PCM data size */
    uint32_t data_remaining; /* bytes left to read */
} wav_reader_t;

/*
 * Open a WAV file for reading.  Parses the RIFF/WAV header.
 * Only PCM format (format tag 1) is supported.
 */
aprs_err_t wav_open(wav_reader_t *w, const char *path);

/*
 * Read samples as int16_t (mono).  For stereo files, channels are
 * averaged to mono.  For 8-bit files, samples are scaled to 16-bit.
 * Returns APRS_OK with *nread=0 at EOF.
 */
aprs_err_t wav_read(wav_reader_t *w, int16_t *buf, size_t max_samples,
                    size_t *nread);

void wav_close(wav_reader_t *w);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_WAV_H */
