/*
 * modem.c — AFSK1200 Bell 202 modulator and demodulator
 *
 * Modulator: AX.25 frame → HDLC + bit-stuff + NRZI + AFSK audio
 * Demodulator: audio → correlator → timing recovery → NRZI → HDLC → frames
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "libaprs/modem.h"
#include "libaprs/ax25.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================== */
/* modulator                                                           */
/* ================================================================== */

struct afsk_mod {
    int sample_rate;
    double phase;            /* current oscillator phase (radians) */
    double samples_per_bit;
    double bit_acc;          /* fractional sample accumulator */
    int current_tone;        /* 0=mark, 1=space */
    int preemph;             /* 1=apply 75µs pre-emphasis (default) */
};

afsk_mod_t *afsk_mod_create(int sample_rate)
{
    afsk_mod_t *m;
    if (sample_rate <= 0) return NULL;
    m = (afsk_mod_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->sample_rate = sample_rate;
    m->samples_per_bit = (double)sample_rate / (double)AFSK_BAUD;
    m->current_tone = 0; /* start with mark */
    m->phase = 0.0;
    m->preemph = 1;      /* on by default for flat/SDR transmitters */
    return m;
}

void afsk_mod_destroy(afsk_mod_t *m)
{
    free(m);
}

void afsk_mod_set_preemph(afsk_mod_t *m, bool enable)
{
    if (m) m->preemph = enable ? 1 : 0;
}

/* Generate samples for one bit using fractional accumulator
 * to avoid timing drift at non-integer samples-per-bit rates. */
static size_t mod_one_bit(afsk_mod_t *m, int16_t *out, size_t max)
{
    double freq = m->current_tone ? AFSK_SPACE_HZ : AFSK_MARK_HZ;
    double dphi = 2.0 * M_PI * freq / (double)m->sample_rate;
    size_t nsamples;
    size_t i;

    /* fractional accumulator: determines how many samples this bit gets */
    m->bit_acc += m->samples_per_bit;
    nsamples = (size_t)m->bit_acc;
    m->bit_acc -= (double)nsamples;

    if (nsamples > max) nsamples = max;

    for (i = 0; i < nsamples; i++) {
        out[i] = (int16_t)(sin(m->phase) * 16000.0);
        m->phase += dphi;
        if (m->phase > 2.0 * M_PI)
            m->phase -= 2.0 * M_PI;
    }

    return nsamples;
}

/*
 * NRZI: bit value 0 → toggle tone, bit value 1 → keep tone.
 * Then generate audio for one bit period.
 */
static size_t mod_nrzi_bit(afsk_mod_t *m, int bit, int16_t *out, size_t max)
{
    if (bit == 0)
        m->current_tone ^= 1;
    return mod_one_bit(m, out, max);
}

/*
 * Emit a byte with HDLC bit stuffing.
 * bits are sent LSB first.
 * *ones tracks consecutive 1-bits for stuffing.
 * Returns number of samples written.
 */
static size_t mod_byte_stuffed(afsk_mod_t *m, uint8_t byte,
                               int *ones, int16_t *out, size_t max)
{
    size_t total = 0;
    int i;

    for (i = 0; i < 8; i++) {
        int bit = (byte >> i) & 1;
        size_t n = mod_nrzi_bit(m, bit, out + total, max - total);
        total += n;

        if (bit == 1) {
            (*ones)++;
            if (*ones == 5) {
                /* stuff a zero bit */
                n = mod_nrzi_bit(m, 0, out + total, max - total);
                total += n;
                *ones = 0;
            }
        } else {
            *ones = 0;
        }
    }

    return total;
}

/* Emit a flag byte (0x7E) — no bit stuffing applied to flags. */
static size_t mod_flag(afsk_mod_t *m, int16_t *out, size_t max)
{
    size_t total = 0;
    int i;
    uint8_t flag = HDLC_FLAG;

    for (i = 0; i < 8; i++) {
        int bit = (flag >> i) & 1;
        size_t n = mod_nrzi_bit(m, bit, out + total, max - total);
        total += n;
    }

    return total;
}

aprs_err_t afsk_mod_frame(afsk_mod_t *m,
                          const uint8_t *frame, size_t frame_len,
                          int16_t *out, size_t max_samples,
                          size_t *nsamples)
{
    size_t total = 0;
    size_t i;
    int ones = 0;
    uint16_t fcs;
    uint8_t fcs_lo, fcs_hi;

    if (!m || !frame || !out || !nsamples) return APRS_ERR_INVALID_ARG;

    /* reset modulator state */
    m->phase = 0.0;
    m->current_tone = 0;
    m->bit_acc = 0.0;

    /* silence lead-in: lets the receiver's squelch open before
     * the preamble flags arrive */
    {
        size_t leadin = (size_t)((int64_t)m->sample_rate * AFSK_LEADIN_MS / 1000);
        if (total + leadin > max_samples) leadin = max_samples - total;
        for (size_t j = 0; j < leadin; j++)
            out[total++] = 0;
    }

    /* preamble flags */
    for (i = 0; i < AFSK_PREAMBLE_FLAGS; i++)
        total += mod_flag(m, out + total, max_samples - total);

    /* frame data with bit stuffing */
    for (i = 0; i < frame_len; i++)
        total += mod_byte_stuffed(m, frame[i], &ones,
                                  out + total, max_samples - total);

    /* FCS — compute over the frame, send LSB first, bit-stuffed */
    fcs = ax25_fcs16(frame, frame_len);
    fcs_lo = (uint8_t)(fcs & 0xFF);
    fcs_hi = (uint8_t)((fcs >> 8) & 0xFF);
    total += mod_byte_stuffed(m, fcs_lo, &ones,
                              out + total, max_samples - total);
    total += mod_byte_stuffed(m, fcs_hi, &ones,
                              out + total, max_samples - total);

    /* closing flags */
    for (i = 0; i < AFSK_POSTAMBLE_FLAGS; i++)
        total += mod_flag(m, out + total, max_samples - total);

    /* Apply 75µs pre-emphasis so the AFSK tones arrive flat after
     * the receiver's standard FM de-emphasis filter.  Without this,
     * the 2200 Hz space tone is ~3-4 dB down from the 1200 Hz mark
     * after de-emphasis, which prevents real FM radios from decoding.
     *
     * First-order FIR pre-emphasis: y[n] = x[n] + α·(x[n] - x[n-1])
     * where α = τ·fs, τ = 75µs.  This is the discrete approximation
     * of H(s) = 1 + s·τ.  The result is normalized to keep the same
     * peak amplitude. */
    if (m->preemph) {
        double alpha = 75.0e-6 * (double)m->sample_rate;
        double prev_x = 0.0;
        double peak = 0.0;

        /* pre-emphasis pass (skip silence lead-in) */
        size_t data_start = (size_t)((int64_t)m->sample_rate
                                      * AFSK_LEADIN_MS / 1000);
        if (data_start > total) data_start = total;

        for (size_t j = data_start; j < total; j++) {
            double x = (double)out[j];
            double y = x + alpha * (x - prev_x);
            prev_x = x;
            double a = fabs(y);
            if (a > peak) peak = a;
            out[j] = (int16_t)y;  /* temporary — will normalize */
        }

        /* normalize to original peak amplitude (16000) so the
         * caller's tx_level scaling still works as expected */
        if (peak > 1.0) {
            double norm = 16000.0 / peak;
            for (size_t j = data_start; j < total; j++)
                out[j] = (int16_t)((double)out[j] * norm);
        }
    }

    *nsamples = total;
    return APRS_OK;
}

/* ================================================================== */
/* demodulator — I/Q correlator + RRC FIR + per-tone AGC + PLL        */
/* ================================================================== */

/*
 * 1. I/Q mix input with continuous-phase mark/space oscillators
 * 2. FIR low-pass filter each I/Q channel (RRC, 2.8 symbol width)
 * 3. Compute magnitude sqrt(I²+Q²) per tone
 * 4. Per-tone AGC: independent peak/valley tracking, normalize to ±0.5
 * 5. Compare normalized mark - space for bit decision
 * 6. 32-bit PLL with multiplicative inertia nudge on transitions
 * 7. NRZI decode → HDLC state machine
 */

#define DEMOD_FIR_MAX  256  /* max FIR filter taps */
#define DEMOD_BP_MAX   512  /* max bandpass prefilter taps */
#define DEMOD_NSLICE     8  /* number of parallel slicers */

/* each slicer has its own PLL, HDLC state, and space_gain */
typedef struct {
    float space_gain;
    int32_t pll;
    int last_demod_sign;
    int last_bit;

    int hdlc_ones;
    int hdlc_bits;
    uint8_t hdlc_byte;
    uint8_t hdlc_buf[HDLC_MAX_FRAME + 2];
    size_t hdlc_len;
    bool hdlc_active;
} demod_slicer_t;

struct afsk_demod {
    int sample_rate;
    double samples_per_bit;

    /* bandpass prefilter */
    float *bp_filter;
    float *bp_hist;         /* input history ring for prefilter */
    int bp_taps;
    int bp_pos;

    /* oscillator phase accumulators */
    uint32_t m_osc_phase, m_osc_delta;
    uint32_t s_osc_phase, s_osc_delta;

    /* RRC low-pass filter */
    float *lp_filter;
    int lp_taps;

    /* I/Q sample history for FIR convolution */
    float *m_I_raw, *m_Q_raw;
    float *s_I_raw, *s_Q_raw;
    int fir_pos;

    /* per-tone AGC */
    float m_peak, m_valley;
    float s_peak, s_valley;
    float agc_fast_attack;
    float agc_slow_decay;

    /* PLL step (shared, slicers have own PLL counters) */
    int32_t pll_step;
    float pll_inertia;

    /* multi-slicer */
    int nslice;
    demod_slicer_t slice[DEMOD_NSLICE];

    /* dedup ring to prevent duplicate frames from different slicers */
    uint32_t dedup[16];
    int dedup_head;

    /* output */
    afsk_frame_cb cb;
    void *cb_user;
};

/* 256-entry sine/cosine lookup */
static float sin256_table[256];
static float cos256_table[256];
static pthread_once_t trig_once = PTHREAD_ONCE_INIT;

static void init_trig_tables(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        double a = (double)i / 256.0 * 2.0 * M_PI;
        sin256_table[i] = (float)sin(a);
        cos256_table[i] = (float)cos(a);
    }
}

static inline float fsin256(uint32_t phase) {
    return sin256_table[(phase >> 24) & 0xFF];
}
static inline float fcos256(uint32_t phase) {
    return cos256_table[(phase >> 24) & 0xFF];
}

/* Generate a windowed-sinc bandpass FIR filter */
static void gen_bandpass(float *filt, int taps, double f_lo, double f_hi,
                         double sample_rate)
{
    int i;
    double center = (double)(taps - 1) / 2.0;
    double sum = 0.0;
    double wl = 2.0 * M_PI * f_lo / sample_rate;
    double wh = 2.0 * M_PI * f_hi / sample_rate;

    for (i = 0; i < taps; i++) {
        double n = (double)i - center;
        double v;
        if (fabs(n) < 0.001) {
            v = (wh - wl) / M_PI;
        } else {
            v = (sin(wh * n) - sin(wl * n)) / (M_PI * n);
        }
        /* Hamming window */
        v *= 0.54 - 0.46 * cos(2.0 * M_PI * (double)i / (double)(taps - 1));
        filt[i] = (float)v;
        sum += v;
    }
    /* normalize for unity passband gain */
    if (fabs(sum) > 0.001) {
        for (i = 0; i < taps; i++)
            filt[i] /= (float)sum;
    }
}

/* Generate Root Raised Cosine FIR filter coefficients */
static void gen_rrc_filter(float *filt, int taps, double symbols,
                           double rolloff, double samples_per_sym)
{
    int i;
    double center = (double)(taps - 1) / 2.0;
    double sum = 0.0;

    for (i = 0; i < taps; i++) {
        double t = ((double)i - center) / samples_per_sym; /* in symbol units */
        double v;

        if (fabs(t) < 0.001) {
            v = 1.0;
        } else if (rolloff > 0.0 && fabs(fabs(t) - 0.5 / rolloff) < 0.001) {
            v = (M_PI / 4.0) * sin(M_PI * t) / (M_PI * t);
        } else {
            double sinc = sin(M_PI * t) / (M_PI * t);
            double rc = cos(M_PI * rolloff * t)
                      / (1.0 - 4.0 * rolloff * rolloff * t * t);
            v = sinc * rc;
        }
        filt[i] = (float)v;
        sum += v;
    }
    /* normalize */
    if (sum != 0.0) {
        for (i = 0; i < taps; i++)
            filt[i] /= (float)sum;
    }

    (void)symbols;
}

/* FIR convolution: dot product of data[0..taps-1] with filter[0..taps-1] */
static inline float fir_convolve(const float *data, const float *filter,
                                 int taps, int pos)
{
    float sum = 0.0f;
    int i, idx;
    for (i = 0; i < taps; i++) {
        idx = (pos - i + taps * 2) % taps;  /* ring buffer index */
        sum += data[idx] * filter[i];
    }
    return sum;
}

/* Per-tone AGC with peak/valley tracking */
static float agc(float in, float fast_attack, float slow_decay,
                 float *ppeak, float *pvalley)
{
    if (in >= *ppeak)
        *ppeak = in * fast_attack + *ppeak * (1.0f - fast_attack);
    else
        *ppeak = in * slow_decay + *ppeak * (1.0f - slow_decay);

    if (in <= *pvalley)
        *pvalley = in * fast_attack + *pvalley * (1.0f - fast_attack);
    else
        *pvalley = in * slow_decay + *pvalley * (1.0f - slow_decay);

    if (*ppeak > *pvalley)
        return (in - 0.5f * (*ppeak + *pvalley)) / (*ppeak - *pvalley);
    return 0.0f;
}

afsk_demod_t *afsk_demod_create(int sample_rate,
                                afsk_frame_cb cb, void *user)
{
    afsk_demod_t *d;
    double spb, rrc_sym;
    int taps, bp_taps, si;

    if (sample_rate <= 0) return NULL;

    pthread_once(&trig_once, init_trig_tables);

    d = (afsk_demod_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;

    d->sample_rate = sample_rate;
    spb = (double)sample_rate / (double)AFSK_BAUD;
    d->samples_per_bit = spb;

    /* --- bandpass prefilter: 1014 - 2386 Hz --- */
    {
        double margin = 0.155 * (double)AFSK_BAUD; /* ~186 Hz */
        double f_lo = (double)AFSK_MARK_HZ - margin;
        double f_hi = (double)AFSK_SPACE_HZ + margin;
        /* filter length: ~8 symbols */
        bp_taps = ((int)(8.0 * spb)) | 1;
        if (bp_taps < 3) bp_taps = 3;
        if (bp_taps > DEMOD_BP_MAX) bp_taps = DEMOD_BP_MAX;

        d->bp_taps = bp_taps;
        d->bp_filter = (float *)calloc((size_t)bp_taps, sizeof(float));
        d->bp_hist   = (float *)calloc((size_t)bp_taps, sizeof(float));
        if (!d->bp_filter || !d->bp_hist) {
            afsk_demod_destroy(d);
            return NULL;
        }
        gen_bandpass(d->bp_filter, bp_taps, f_lo, f_hi,
                     (double)sample_rate);
    }

    /* oscillator phase deltas */
    d->m_osc_delta = (uint32_t)(0x100000000ULL * AFSK_MARK_HZ / sample_rate);
    d->s_osc_delta = (uint32_t)(0x100000000ULL * AFSK_SPACE_HZ / sample_rate);

    /* RRC low-pass filter: 2.80 symbol width, rolloff 0.20 */
    rrc_sym = 2.80;
    taps = ((int)(rrc_sym * spb)) | 1;
    if (taps < 3) taps = 3;
    if (taps > DEMOD_FIR_MAX) taps = DEMOD_FIR_MAX;
    d->lp_taps = taps;

    d->lp_filter = (float *)calloc((size_t)taps, sizeof(float));
    d->m_I_raw = (float *)calloc((size_t)taps, sizeof(float));
    d->m_Q_raw = (float *)calloc((size_t)taps, sizeof(float));
    d->s_I_raw = (float *)calloc((size_t)taps, sizeof(float));
    d->s_Q_raw = (float *)calloc((size_t)taps, sizeof(float));

    if (!d->lp_filter || !d->m_I_raw || !d->m_Q_raw ||
        !d->s_I_raw || !d->s_Q_raw) {
        afsk_demod_destroy(d);
        return NULL;
    }

    gen_rrc_filter(d->lp_filter, taps, rrc_sym, 0.20, spb);

    /* per-tone AGC */
    d->agc_fast_attack = 0.70f;
    d->agc_slow_decay  = 0.000090f;

    /* PLL inertia — smoothly scale with samples-per-bit.
     * Low rates (few samples per bit) need aggressive correction,
     * high rates can be more conservative to avoid jitter. */
    d->pll_step = (int32_t)(0x100000000ULL * AFSK_BAUD / sample_rate);
    {
        /* sqrt ramp: faster rise at low spb, flattens at high spb */
        double t = (spb - 7.0) / 33.0;
        double inertia;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        inertia = 0.50 + 0.24 * sqrt(t);
        d->pll_inertia = (float)inertia;
    }

    /* multi-slicer: 8 parallel slicers with logarithmically
     * spaced space_gain from 0.5 to 4.0 */
    d->nslice = DEMOD_NSLICE;
    for (si = 0; si < DEMOD_NSLICE; si++) {
        /* gain = 0.5 * (4.0/0.5)^(i/(N-1)) = 0.5 * 8^(i/7) */
        double g = 0.5 * pow(8.0, (double)si / (double)(DEMOD_NSLICE - 1));
        d->slice[si].space_gain = (float)g;
    }

    d->cb = cb;
    d->cb_user = user;

    return d;
}

void afsk_demod_destroy(afsk_demod_t *d)
{
    if (!d) return;
    free(d->bp_filter);
    free(d->bp_hist);
    free(d->lp_filter);
    free(d->m_I_raw);
    free(d->m_Q_raw);
    free(d->s_I_raw);
    free(d->s_Q_raw);
    free(d);
}

static uint32_t frame_fnv(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

/* Emit a frame — dedup across slicers, then callback */
static void demod_emit(afsk_demod_t *d, const uint8_t *frame, size_t len)
{
    uint32_t h;
    int i;

    h = frame_fnv(frame, len);
    for (i = 0; i < 16; i++) {
        if (d->dedup[i] == h) return; /* already emitted */
    }
    d->dedup[d->dedup_head] = h;
    d->dedup_head = (d->dedup_head + 1) & 15;

    if (d->cb)
        d->cb(frame, len, d->cb_user);
}

/* Try to emit a completed HDLC frame.
 * Verify FCS.  On failure, try single-bit FEC (flip each bit). */
static void hdlc_emit(demod_slicer_t *sl, afsk_demod_t *d)
{
    uint16_t fcs_recv, fcs_calc;
    size_t frame_len;

    if (sl->hdlc_len < 3) { sl->hdlc_len = 0; return; }

    frame_len = sl->hdlc_len - 2;  /* exclude FCS bytes */

    fcs_recv = (uint16_t)sl->hdlc_buf[sl->hdlc_len - 2]
             | ((uint16_t)sl->hdlc_buf[sl->hdlc_len - 1] << 8);
    fcs_calc = ax25_fcs16(sl->hdlc_buf, frame_len);

    if (fcs_recv == fcs_calc) {
        demod_emit(d, sl->hdlc_buf, frame_len);
        sl->hdlc_len = 0;
        return;
    }

    /* single-bit FEC: try flipping each bit */
    {
        size_t total_bits = sl->hdlc_len * 8;
        size_t b;
        for (b = 0; b < total_bits; b++) {
            sl->hdlc_buf[b >> 3] ^= (uint8_t)(1 << (b & 7));

            fcs_recv = (uint16_t)sl->hdlc_buf[sl->hdlc_len - 2]
                     | ((uint16_t)sl->hdlc_buf[sl->hdlc_len - 1] << 8);
            fcs_calc = ax25_fcs16(sl->hdlc_buf, frame_len);

            if (fcs_recv == fcs_calc) {
                demod_emit(d, sl->hdlc_buf, frame_len);
                sl->hdlc_len = 0;
                return;
            }

            sl->hdlc_buf[b >> 3] ^= (uint8_t)(1 << (b & 7)); /* undo */
        }
    }

    sl->hdlc_len = 0;
}

static void sl_data_bit(demod_slicer_t *sl, int bit)
{
    if (!sl->hdlc_active) return;
    if (bit)
        sl->hdlc_byte |= (uint8_t)(1 << sl->hdlc_bits);
    sl->hdlc_bits++;
    if (sl->hdlc_bits == 8) {
        if (sl->hdlc_len < HDLC_MAX_FRAME + 2)
            sl->hdlc_buf[sl->hdlc_len++] = sl->hdlc_byte;
        sl->hdlc_bits = 0;
        sl->hdlc_byte = 0;
    }
}

static void sl_hdlc_bit(demod_slicer_t *sl, int bit, afsk_demod_t *d)
{
    if (bit == 1) {
        sl->hdlc_ones++;
        return;
    }

    if (sl->hdlc_ones >= 7) {
        sl->hdlc_active = false;
        sl->hdlc_len = 0;
        sl->hdlc_ones = 0;
        return;
    }

    if (sl->hdlc_ones == 6) {
        if (sl->hdlc_active && sl->hdlc_len > 0)
            hdlc_emit(sl, d);
        sl->hdlc_active = true;
        sl->hdlc_bits = 0;
        sl->hdlc_byte = 0;
        sl->hdlc_len = 0;
        sl->hdlc_ones = 0;
        return;
    }

    if (sl->hdlc_ones == 5) {
        int j;
        for (j = 0; j < 5; j++)
            sl_data_bit(sl, 1);
        sl->hdlc_ones = 0;
        return;
    }

    {
        int j;
        for (j = 0; j < sl->hdlc_ones; j++)
            sl_data_bit(sl, 1);
        sl_data_bit(sl, 0);
        sl->hdlc_ones = 0;
    }
}

aprs_err_t afsk_demod_feed(afsk_demod_t *d,
                           const int16_t *samples, size_t count)
{
    size_t i;
    int lp_taps, si;

    if (!d) return APRS_ERR_INVALID_ARG;
    if (!samples && count > 0) return APRS_ERR_INVALID_ARG;

    lp_taps = d->lp_taps;

    for (i = 0; i < count; i++) {
        float fsam = (float)samples[i] / 32768.0f;
        float filtered;
        float m_I, m_Q, s_I, s_Q;
        float m_amp, s_amp;
        int fp;

        /* bandpass prefilter */
        d->bp_hist[d->bp_pos] = fsam;
        filtered = fir_convolve(d->bp_hist, d->bp_filter,
                                d->bp_taps, d->bp_pos);
        d->bp_pos = (d->bp_pos + 1) % d->bp_taps;

        /* I/Q mixing with 256-entry lookup oscillators */
        fp = d->fir_pos;

        d->m_I_raw[fp] = filtered * fcos256(d->m_osc_phase);
        d->m_Q_raw[fp] = filtered * fsin256(d->m_osc_phase);
        d->s_I_raw[fp] = filtered * fcos256(d->s_osc_phase);
        d->s_Q_raw[fp] = filtered * fsin256(d->s_osc_phase);

        d->m_osc_phase += d->m_osc_delta;
        d->s_osc_phase += d->s_osc_delta;

        d->fir_pos = (fp + 1) % lp_taps;

        /* FIR low-pass filter each I/Q channel */
        m_I = fir_convolve(d->m_I_raw, d->lp_filter, lp_taps, fp);
        m_Q = fir_convolve(d->m_Q_raw, d->lp_filter, lp_taps, fp);
        s_I = fir_convolve(d->s_I_raw, d->lp_filter, lp_taps, fp);
        s_Q = fir_convolve(d->s_Q_raw, d->lp_filter, lp_taps, fp);

        /* magnitude */
        m_amp = hypotf(m_I, m_Q);
        s_amp = hypotf(s_I, s_Q);

        /* per-tone AGC (shared across slicers) */
        agc(m_amp, d->agc_fast_attack, d->agc_slow_decay,
            &d->m_peak, &d->m_valley);
        agc(s_amp, d->agc_fast_attack, d->agc_slow_decay,
            &d->s_peak, &d->s_valley);

        /* run each slicer with its own space_gain and PLL */
        for (si = 0; si < d->nslice; si++) {
            demod_slicer_t *sl = &d->slice[si];
            float demod_out;
            int demod_sign;
            int32_t prev_pll;

            /* slicer decision: mark_amp - space_amp * gain */
            demod_out = m_amp - s_amp * sl->space_gain;
            demod_sign = (demod_out > 0.0f) ? 1 : 0;

            /* PLL */
            prev_pll = sl->pll;
            sl->pll += d->pll_step;

            if (demod_sign != sl->last_demod_sign) {
                sl->pll = (int32_t)(sl->pll * d->pll_inertia);
            }
            sl->last_demod_sign = demod_sign;

            /* bit clock fires on overflow */
            if (prev_pll >= 0 && sl->pll < 0) {
                int raw_bit = demod_sign;
                int decoded = (raw_bit == sl->last_bit) ? 1 : 0;
                sl->last_bit = raw_bit;
                sl_hdlc_bit(sl, decoded, d);
            }
        }
    }

    return APRS_OK;
}
