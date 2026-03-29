/*
 * wav.c — simple PCM WAV file reader
 */

#include <string.h>
#include <stdlib.h>
#include "libaprs/wav.h"

/* read little-endian integers from file */
static int read_u16(FILE *fp, uint16_t *out)
{
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return -1;
    *out = (uint16_t)(b[0] | (b[1] << 8));
    return 0;
}

static int read_u32(FILE *fp, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return -1;
    *out = (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    return 0;
}

aprs_err_t wav_open(wav_reader_t *w, const char *path)
{
    char tag[4];
    uint32_t chunk_size;
    uint16_t fmt_tag, channels, bits;
    uint32_t sample_rate, byte_rate;
    uint16_t block_align;

    if (!w || !path) return APRS_ERR_INVALID_ARG;
    memset(w, 0, sizeof(*w));

    w->fp = fopen(path, "rb");
    if (!w->fp) return APRS_ERR_IO;

    /* RIFF header */
    if (fread(tag, 1, 4, w->fp) != 4 || memcmp(tag, "RIFF", 4) != 0)
        goto fail;
    if (read_u32(w->fp, &chunk_size) < 0) goto fail;

    if (fread(tag, 1, 4, w->fp) != 4 || memcmp(tag, "WAVE", 4) != 0)
        goto fail;

    /* find fmt and data chunks */
    w->sample_rate = 0;
    w->data_bytes = 0;

    while (1) {
        if (fread(tag, 1, 4, w->fp) != 4) break;
        if (read_u32(w->fp, &chunk_size) < 0) break;

        if (memcmp(tag, "fmt ", 4) == 0) {
            if (read_u16(w->fp, &fmt_tag) < 0) goto fail;
            if (fmt_tag != 1) goto fail; /* PCM only */
            if (read_u16(w->fp, &channels) < 0) goto fail;
            if (read_u32(w->fp, &sample_rate) < 0) goto fail;
            if (read_u32(w->fp, &byte_rate) < 0) goto fail;
            (void)byte_rate;
            if (read_u16(w->fp, &block_align) < 0) goto fail;
            (void)block_align;
            if (read_u16(w->fp, &bits) < 0) goto fail;

            w->sample_rate = (int)sample_rate;
            w->channels = (int)channels;
            w->bits_per_sample = (int)bits;

            /* skip any extra fmt bytes */
            if (chunk_size > 16)
                fseek(w->fp, (long)(chunk_size - 16), SEEK_CUR);

        } else if (memcmp(tag, "data", 4) == 0) {
            w->data_bytes = chunk_size;
            w->data_remaining = chunk_size;
            break; /* positioned at start of PCM data */

        } else {
            /* skip unknown chunk */
            fseek(w->fp, (long)chunk_size, SEEK_CUR);
        }
    }

    if (w->sample_rate == 0 || w->data_bytes == 0)
        goto fail;

    return APRS_OK;

fail:
    fclose(w->fp);
    w->fp = NULL;
    return APRS_ERR_PARSE;
}

aprs_err_t wav_read(wav_reader_t *w, int16_t *buf, size_t max_samples,
                    size_t *nread)
{
    size_t i, got;
    int bytes_per_frame;

    if (!w || !w->fp || !buf || !nread) return APRS_ERR_INVALID_ARG;

    *nread = 0;
    if (w->data_remaining == 0) return APRS_OK;

    bytes_per_frame = w->channels * (w->bits_per_sample / 8);

    if (w->bits_per_sample == 16 && w->channels == 1) {
        /* fast path: 16-bit mono — read directly */
        size_t want = max_samples;
        size_t byte_want = want * 2;
        if (byte_want > w->data_remaining)
            byte_want = w->data_remaining;
        want = byte_want / 2;

        got = fread(buf, 2, want, w->fp);
        w->data_remaining -= (uint32_t)(got * 2);
        *nread = got;

    } else if (w->bits_per_sample == 16 && w->channels == 2) {
        /* 16-bit stereo — average channels */
        int16_t pair[2];
        for (i = 0; i < max_samples && w->data_remaining >= 4; i++) {
            if (fread(pair, 2, 2, w->fp) != 2) break;
            w->data_remaining -= 4;
            buf[i] = (int16_t)(((int)pair[0] + (int)pair[1]) / 2);
        }
        *nread = i;

    } else if (w->bits_per_sample == 8 && w->channels == 1) {
        /* 8-bit mono — scale to 16-bit */
        uint8_t b;
        for (i = 0; i < max_samples && w->data_remaining > 0; i++) {
            if (fread(&b, 1, 1, w->fp) != 1) break;
            w->data_remaining--;
            buf[i] = (int16_t)((int)b - 128) << 8;
        }
        *nread = i;

    } else if (w->bits_per_sample == 8 && w->channels == 2) {
        /* 8-bit stereo */
        uint8_t pair[2];
        for (i = 0; i < max_samples && w->data_remaining >= 2; i++) {
            if (fread(pair, 1, 2, w->fp) != 2) break;
            w->data_remaining -= 2;
            int avg = ((int)pair[0] + (int)pair[1]) / 2 - 128;
            buf[i] = (int16_t)(avg << 8);
        }
        *nread = i;

    } else {
        return APRS_ERR_UNSUPPORTED;
    }

    return APRS_OK;
}

void wav_close(wav_reader_t *w)
{
    if (!w) return;
    if (w->fp) {
        fclose(w->fp);
        w->fp = NULL;
    }
}
