#ifndef LIBAPRS_MODEM_H
#define LIBAPRS_MODEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bell 202 AFSK parameters */
#define AFSK_MARK_HZ      1200
#define AFSK_SPACE_HZ     2200
#define AFSK_BAUD          1200

/* HDLC */
#define HDLC_FLAG          0x7E
#define HDLC_MAX_FRAME     1024

/* silence before preamble for receiver squelch lead-in (ms) */
#define AFSK_LEADIN_MS        500
/* default preamble: number of flag bytes before data */
#define AFSK_PREAMBLE_FLAGS   100  /* ~667ms at 1200 baud — long preamble for reliable PLL lock */
/* postamble flags */
#define AFSK_POSTAMBLE_FLAGS   5

/* ------------------------------------------------------------------ */
/* modulator                                                           */
/* ------------------------------------------------------------------ */

typedef struct afsk_mod afsk_mod_t;

afsk_mod_t *afsk_mod_create(int sample_rate);
void        afsk_mod_destroy(afsk_mod_t *m);

/* Disable 75µs pre-emphasis (default: on).
 * Turn OFF when feeding into a radio's mic/data input — the radio's
 * FM modulator already applies pre-emphasis.  Leave ON for flat/SDR
 * transmitters that skip analog pre-emphasis. */
void        afsk_mod_set_preemph(afsk_mod_t *m, bool enable);

/*
 * Modulate an AX.25 frame (without FCS — FCS is computed and appended).
 * Outputs audio samples in out[].
 *   frame       — raw AX.25 frame bytes (addresses + control + PID + info)
 *   frame_len   — length of frame
 *   out         — output sample buffer
 *   max_samples — size of out in samples
 *   nsamples    — actual number of samples written
 *
 * The output includes preamble flags, HDLC-framed bit-stuffed data
 * with NRZI encoding, and postamble flags.
 */
aprs_err_t afsk_mod_frame(afsk_mod_t *m,
                          const uint8_t *frame, size_t frame_len,
                          int16_t *out, size_t max_samples,
                          size_t *nsamples);

/* ------------------------------------------------------------------ */
/* demodulator                                                         */
/* ------------------------------------------------------------------ */

/*
 * Callback when a complete, FCS-verified AX.25 frame is decoded.
 * The frame bytes do NOT include the FCS.
 */
typedef void (*afsk_frame_cb)(const uint8_t *frame, size_t frame_len,
                              void *user);

typedef struct afsk_demod afsk_demod_t;

afsk_demod_t *afsk_demod_create(int sample_rate,
                                afsk_frame_cb cb, void *user);
void          afsk_demod_destroy(afsk_demod_t *d);

/*
 * Feed audio samples into the demodulator.
 * May invoke the callback zero or more times.
 */
aprs_err_t afsk_demod_feed(afsk_demod_t *d,
                           const int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_MODEM_H */
