/*
 * gen_test_wavs — generate WAV files for every APRS packet type
 *
 * For each supported packet type, builds a realistic TNC2 line,
 * parses it, encodes it as an AX.25 UI frame, modulates it with
 * Bell 202 AFSK at 1200 baud, and writes a 16-bit mono WAV file.
 *
 * Usage: gen_test_wavs [output_directory] [sample_rate]
 *        (defaults to current directory, 48000 Hz)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "libaprs/aprs.h"
#include "libaprs/ax25.h"
#include "libaprs/modem.h"

static int g_sample_rate = 48000;
#define MAX_SAMPLES  (48000 * 4)   /* 4 seconds max at highest rate */

/* ------------------------------------------------------------------ */
/* minimal WAV writer                                                  */
/* ------------------------------------------------------------------ */

static int write_wav(const char *path, const int16_t *pcm,
                     size_t nsamples, int rate)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    uint32_t data_bytes = (uint32_t)(nsamples * 2);
    uint32_t riff_size  = 36 + data_bytes;
    uint16_t fmt_tag    = 1;       /* PCM */
    uint16_t channels   = 1;
    uint32_t srate      = (uint32_t)rate;
    uint32_t byte_rate  = srate * 2;
    uint16_t block_align = 2;
    uint16_t bits       = 16;
    uint32_t fmt_size   = 16;

    fwrite("RIFF", 1, 4, fp);
    fwrite(&riff_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    fwrite(&fmt_size, 4, 1, fp);
    fwrite(&fmt_tag, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    fwrite(&srate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_bytes, 4, 1, fp);
    fwrite(pcm, 2, nsamples, fp);

    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* modulate a TNC2 line to a WAV file                                  */
/* ------------------------------------------------------------------ */

static int tnc2_to_wav(const char *tnc2, const char *wav_path,
                       const char *label)
{
    aprs_packet_t pkt;
    ax25_ui_frame_t frame;
    uint8_t ax25_buf[1024];
    size_t ax25_len;
    int16_t *samples;
    size_t nsamples;
    afsk_mod_t *mod;
    aprs_err_t rc;

    aprs_packet_init(&pkt);

    rc = aprs_parse_tnc2(tnc2, &pkt);
    if (rc != APRS_OK) {
        fprintf(stderr, "  [%s] parse failed: %s\n    line: %s\n",
                label, aprs_strerror(rc), tnc2);
        return -1;
    }

    rc = ax25_from_aprs(&pkt, &frame);
    if (rc != APRS_OK) {
        fprintf(stderr, "  [%s] ax25_from_aprs failed: %s\n",
                label, aprs_strerror(rc));
        return -1;
    }

    rc = ax25_encode_ui_frame(&frame, ax25_buf, sizeof(ax25_buf), &ax25_len);
    if (rc != APRS_OK) {
        fprintf(stderr, "  [%s] ax25_encode failed: %s\n",
                label, aprs_strerror(rc));
        return -1;
    }

    mod = afsk_mod_create(g_sample_rate);
    if (!mod) {
        fprintf(stderr, "  [%s] modulator create failed\n", label);
        return -1;
    }

    samples = malloc(MAX_SAMPLES * sizeof(int16_t));
    if (!samples) {
        afsk_mod_destroy(mod);
        return -1;
    }

    rc = afsk_mod_frame(mod, ax25_buf, ax25_len,
                        samples, MAX_SAMPLES, &nsamples);
    afsk_mod_destroy(mod);

    if (rc != APRS_OK) {
        fprintf(stderr, "  [%s] modulation failed: %s\n",
                label, aprs_strerror(rc));
        free(samples);
        return -1;
    }

    if (write_wav(wav_path, samples, nsamples, g_sample_rate) != 0) {
        fprintf(stderr, "  [%s] write_wav failed: %s\n", label, wav_path);
        free(samples);
        return -1;
    }

    double dur = (double)nsamples / g_sample_rate;
    printf("  %-22s %5zu samples  %.2fs  %s\n", label, nsamples, dur, wav_path);

    free(samples);
    return 0;
}

/* ------------------------------------------------------------------ */
/* main — generate one WAV per packet type                             */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *dir = ".";
    if (argc > 1) dir = argv[1];
    if (argc > 2) g_sample_rate = atoi(argv[2]);

    char path[512];
    int ok = 0, fail = 0;

    printf("Generating APRS test WAV files (%d Hz mono) in %s/\n\n", g_sample_rate, dir);

    /* ---- Position (no messaging) ---- */
    snprintf(path, sizeof(path), "%s/position.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-9>APRS,WIDE1-1,WIDE2-1:!3521.00N/09730.00W>Mobile in OKC",
        path, "position") == 0) ok++; else fail++;

    /* ---- Position with messaging ---- */
    snprintf(path, sizeof(path), "%s/position_msgcap.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-5>APRS,WIDE1-1:=3521.00N/09730.00W-IGate/Digi",
        path, "position_msgcap") == 0) ok++; else fail++;

    /* ---- Message ---- */
    snprintf(path, sizeof(path), "%s/message.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-9>APRS,WIDE1-1::W3ADO-1  :Hello from libaprs!{001",
        path, "message") == 0) ok++; else fail++;

    /* ---- Message ACK ---- */
    snprintf(path, sizeof(path), "%s/message_ack.wav", dir);
    if (tnc2_to_wav(
        "W3ADO-1>APRS::N0CALL-9 :ack001",
        path, "message_ack") == 0) ok++; else fail++;

    /* ---- Message REJ ---- */
    snprintf(path, sizeof(path), "%s/message_rej.wav", dir);
    if (tnc2_to_wav(
        "W3ADO-1>APRS::N0CALL-9 :rej001",
        path, "message_rej") == 0) ok++; else fail++;

    /* ---- Status ---- */
    snprintf(path, sizeof(path), "%s/status.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-9>APRS,WIDE2-1:>Libaprs test station operational",
        path, "status") == 0) ok++; else fail++;

    /* ---- Object ---- */
    snprintf(path, sizeof(path), "%s/object.wav", dir);
    if (tnc2_to_wav(
        "N0CALL>APRS:;BALLOON  *092345z3521.00N/09730.00WO/A=065000",
        path, "object") == 0) ok++; else fail++;

    /* ---- Object (killed) ---- */
    snprintf(path, sizeof(path), "%s/object_killed.wav", dir);
    if (tnc2_to_wav(
        "N0CALL>APRS:;BALLOON  _092345z3521.00N/09730.00WO",
        path, "object_killed") == 0) ok++; else fail++;

    /* ---- Item ---- */
    snprintf(path, sizeof(path), "%s/item.wav", dir);
    if (tnc2_to_wav(
        "N0CALL>APRS:)REPEATER!3521.00N/09730.00W#PHG2360/W3,OKn",
        path, "item") == 0) ok++; else fail++;

    /* ---- Weather ---- */
    snprintf(path, sizeof(path), "%s/weather.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-13>APRS:@092345z3521.00N/09730.00W_090/005g010t077r001p010P010h85b10135",
        path, "weather") == 0) ok++; else fail++;

    /* ---- Telemetry ---- */
    snprintf(path, sizeof(path), "%s/telemetry.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-11>APRS:T#005,199,100,050,025,012,10110000,Battery OK",
        path, "telemetry") == 0) ok++; else fail++;

    /* ---- Query ---- */
    snprintf(path, sizeof(path), "%s/query.wav", dir);
    if (tnc2_to_wav(
        "N0CALL>APRS:?APRS?",
        path, "query") == 0) ok++; else fail++;

    /* ---- Third-party ---- */
    snprintf(path, sizeof(path), "%s/thirdparty.wav", dir);
    if (tnc2_to_wav(
        "N0CALL>APRS:}W3ADO-1>APRS,TCPIP*:!3521.00N/09730.00W>Relayed",
        path, "thirdparty") == 0) ok++; else fail++;

    /* ---- Mic-E ---- */
    snprintf(path, sizeof(path), "%s/mic_e.wav", dir);
    if (tnc2_to_wav(
        "N0CALL-9>T5SPRQ:`(_fn\"Oj/",
        path, "mic_e") == 0) ok++; else fail++;

    printf("\nDone: %d generated, %d failed\n", ok, fail);

    return fail > 0 ? 1 : 0;
}
