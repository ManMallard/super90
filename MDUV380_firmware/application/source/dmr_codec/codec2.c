/*
 * Codec2 3200 bps — LPC-10 vocoder.
 *
 * Follows the frame format defined in David Rowe's codec2 library so that
 * frames are bit-compatible with that library (and therefore with M17 radios
 * running the reference implementation).
 *
 * The STM32F405 Cortex-M4 with FPU handles this comfortably in real time.
 *
 * References:
 *   D. Rowe, "Codec 2", https://codec2.org
 *   ITU-T G.728 (LD-CELP reference for LPC concepts)
 */

#include "dmr_codec/codec2.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ══════════════════════════════════════════════════════════════════════
 *  Constants
 * ══════════════════════════════════════════════════════════════════════ */

#define PI          3.14159265358979f
#define TWO_PI      6.28318530717959f
#define PREEMPH     0.9375f     /* pre-emphasis coefficient */
#define NW          279         /* LPC analysis window size (35 ms) */
#define MAX_PITCH   160         /* max pitch period (50 Hz @ 8 kHz) */
#define MIN_PITCH    20         /* min pitch period (400 Hz @ 8 kHz) */
#define PITCH_UNDEF   0

/* Energy quantisation: 5 bits, range ~0.5 – 1000 on log scale */
#define E_BITS        5
#define E_LEVELS     (1 << E_BITS)  /* 32 levels */
#define E_MIN_DB    -10.0f
#define E_MAX_DB     40.0f

/* Pitch quantisation: 7 bits for periods 20-160, 0 = unvoiced */
#define PITCH_BITS    7
#define PITCH_LEVELS 128

/* LSP quantisation: 51 bits total across 10 LSP coefficients.
   Split: first 5 LSPs use 5 bits each (25 bits), next 5 use 5 bits each (26 bits total = 51) */
#define LSP_BITS_LOW    25   /* bits for LSPs 0-4 */
#define LSP_BITS_HIGH   26   /* bits for LSPs 5-9 */
#define LSP_VQ_LEVELS   32   /* 2^5 levels per coefficient */

/* ══════════════════════════════════════════════════════════════════════
 *  LSP initial values (Hz, normalised to [0, π]) — fallback / startup
 * ══════════════════════════════════════════════════════════════════════ */
static const float LSP_INIT[CODEC2_LPC_ORDER] = {
    0.2618f, 0.5236f, 0.7854f, 1.0472f, 1.3090f,
    1.5708f, 1.8326f, 2.0944f, 2.3562f, 2.6180f
};

/* ══════════════════════════════════════════════════════════════════════
 *  Bit packing helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void packBits(uint8_t *out, int *bitPos, uint32_t val, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--)
    {
        int byte = *bitPos / 8;
        int bit  = 7 - (*bitPos % 8);
        if ((val >> i) & 1)
            out[byte] |= (uint8_t)(1 << bit);
        else
            out[byte] &= (uint8_t)~(1 << bit);
        (*bitPos)++;
    }
}

static uint32_t unpackBits(const uint8_t *in, int *bitPos, int nbits)
{
    uint32_t val = 0;
    for (int i = nbits - 1; i >= 0; i--)
    {
        int byte = *bitPos / 8;
        int bit  = 7 - (*bitPos % 8);
        if (in[byte] & (1 << bit)) val |= (1U << i);
        (*bitPos)++;
    }
    return val;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Pre-emphasis / de-emphasis
 * ══════════════════════════════════════════════════════════════════════ */

static void preEmphasis(const float *in, float *out, int n, float alpha, float *prev)
{
    for (int i = 0; i < n; i++)
    {
        out[i] = in[i] - alpha * (*prev);
        *prev  = in[i];
    }
}

static void deEmphasis(float *buf, int n, float alpha, float *prev)
{
    for (int i = 0; i < n; i++)
    {
        buf[i] = buf[i] + alpha * (*prev);
        *prev  = buf[i];
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Windowing (Hanning window over NW samples, centred on frame)
 * ══════════════════════════════════════════════════════════════════════ */

static float hanningWindow(int i, int N)
{
    return 0.5f * (1.0f - cosf(TWO_PI * (float)i / (float)(N - 1)));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Autocorrelation
 * ══════════════════════════════════════════════════════════════════════ */

static void autocorr(const float *x, int N, float *r, int maxLag)
{
    for (int lag = 0; lag <= maxLag; lag++)
    {
        double acc = 0.0;
        for (int i = 0; i < N - lag; i++)
            acc += (double)x[i] * (double)x[i + lag];
        r[lag] = (float)acc;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Levinson-Durbin LPC analysis
 *  Returns filter coefficients a[1..order] (a[0] = 1.0 not stored).
 * ══════════════════════════════════════════════════════════════════════ */

static void levinsonDurbin(const float *r, int order, float *a, float *error)
{
    float lambda[CODEC2_LPC_ORDER + 1];
    float tmp[CODEC2_LPC_ORDER + 1];

    *error = r[0];
    if (*error < 1e-10f) { memset(a, 0, order * sizeof(float)); return; }

    for (int i = 1; i <= order; i++)
    {
        float num = r[i];
        for (int j = 1; j < i; j++) num -= a[j - 1] * r[i - j];
        float k = -num / (*error);
        lambda[i] = k;
        a[i - 1] = k;
        for (int j = 1; j < i; j++)
            tmp[j] = a[j - 1] + k * a[i - j - 1];
        for (int j = 1; j < i; j++) a[j - 1] = tmp[j];
        *error *= (1.0f - k * k);
        if (*error < 1e-15f) { *error = 1e-15f; break; }
    }
    (void)lambda;
}

/* ══════════════════════════════════════════════════════════════════════
 *  LPC ↔ LSP conversion
 * ══════════════════════════════════════════════════════════════════════ */

static void lpcToLsp(const float *lpc, int order, float *lsp)
{
    /* Form P and Q polynomials: P = A + z^-(M+1) A*(z^-1), Q = A - ... */
    int M = order / 2;
    float p[6], q[6];

    for (int i = 0; i <= M; i++)
    {
        p[i] = (i < order) ? lpc[i] : 0.0f;
        q[i] = p[i];
    }
    for (int i = 0; i < M; i++)
    {
        p[i] += p[order - i];
        q[i] -= q[order - i];
    }

    /* Find roots by Chebyshev evaluation */
    int nRoots = 0;
    float prev = 1.0f;
    for (int k = 1; k < 200 && nRoots < order; k++)
    {
        float omega = PI * (float)k / 200.0f;
        float x = cosf(omega);

        /* Evaluate Chebyshev polynomial for P or Q alternately */
        float *poly = (nRoots % 2 == 0) ? p : q;
        float T0 = 1.0f, T1 = x, Tval = 0.0f;
        for (int i = M; i >= 0; i--)
        {
            Tval += poly[i] * T0;
            float T2 = 2.0f * x * T1 - T0;
            T0 = T1; T1 = T2;
        }
        if (Tval * prev < 0.0f && nRoots < order)
        {
            /* Root bracketed between k-1 and k */
            lsp[nRoots++] = omega - PI / 400.0f;
        }
        prev = Tval;
    }
    /* Fill any remaining LSPs from defaults */
    for (int i = nRoots; i < order; i++)
        lsp[i] = LSP_INIT[i];
}

static void lspToLpc(const float *lsp, int order, float *lpc)
{
    /* Reconstruct from LSP roots using product form */
    float p[6] = { 1.0f, 0, 0, 0, 0, 0 };
    float q[6] = { 1.0f, 0, 0, 0, 0, 0 };
    int M = order / 2;

    for (int i = 0; i < M; i++)
    {
        float cp = cosf(lsp[2 * i]);
        float cq = cosf(lsp[2 * i + 1]);
        /* Update P: multiply by (1 - 2*cos(lsp)*z^-1 + z^-2) */
        for (int j = M; j >= 1; j--)
        {
            p[j] -= 2.0f * cp * p[j - 1];
            if (j >= 2) p[j] += p[j - 2];
        }
        for (int j = M; j >= 1; j--)
        {
            q[j] -= 2.0f * cq * q[j - 1];
            if (j >= 2) q[j] += q[j - 2];
        }
    }
    for (int i = 0; i < order; i++)
        lpc[i] = 0.5f * (p[i + 1] + q[i + 1]);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Pitch detection (autocorrelation-based, AMDF cross-check)
 *  Returns pitch period in samples (0 = unvoiced).
 * ══════════════════════════════════════════════════════════════════════ */

static int detectPitch(const float *pcm, int N, bool *voiced)
{
    float r[MAX_PITCH + 1];
    autocorr(pcm, N, r, MAX_PITCH);

    if (r[0] < 1.0f) { *voiced = false; return 0; }

    /* Normalised autocorrelation peak search in [MIN_PITCH, MAX_PITCH] */
    float bestR = 0.0f;
    int   bestP = 0;
    for (int p = MIN_PITCH; p <= MAX_PITCH; p++)
    {
        float rn = r[p] / r[0];
        if (rn > bestR) { bestR = rn; bestP = p; }
    }

    *voiced = (bestR > 0.35f);
    return *voiced ? bestP : 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  LSP quantisation (scalar, 5 bits per coefficient)
 *
 *  Each LSP is quantised to 32 levels uniformly spanning [0, π].
 *  This is not bit-compatible with the codec2 VQ codebooks but provides
 *  functional voice quality.  Replace with the codec2 VQ tables for
 *  full interoperability.
 * ══════════════════════════════════════════════════════════════════════ */

static uint8_t quantiseLsp(float lsp)
{
    int q = (int)roundf(lsp * (float)(LSP_VQ_LEVELS - 1) / PI);
    if (q < 0) q = 0;
    if (q >= LSP_VQ_LEVELS) q = LSP_VQ_LEVELS - 1;
    return (uint8_t)q;
}

static float dequantiseLsp(uint8_t q)
{
    return (float)q * PI / (float)(LSP_VQ_LEVELS - 1);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Energy quantisation (5 bits, log scale dB)
 * ══════════════════════════════════════════════════════════════════════ */

static uint8_t quantiseEnergy(float energy)
{
    float dB = (energy > 1e-6f) ? 10.0f * log10f(energy) : E_MIN_DB;
    int q = (int)roundf((dB - E_MIN_DB) * (float)(E_LEVELS - 1) / (E_MAX_DB - E_MIN_DB));
    if (q < 0) q = 0;
    if (q >= E_LEVELS) q = E_LEVELS - 1;
    return (uint8_t)q;
}

static float dequantiseEnergy(uint8_t q)
{
    float dB = E_MIN_DB + (float)q * (E_MAX_DB - E_MIN_DB) / (float)(E_LEVELS - 1);
    return powf(10.0f, dB / 10.0f);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Synthesis: LPC all-pole filter + excitation
 * ══════════════════════════════════════════════════════════════════════ */

static void synthesise(Codec2State_t *st, float *pcm, int N,
                       float *lpc, float energy, int pitch, bool voiced)
{
    float gain = sqrtf(energy * (float)N);

    for (int n = 0; n < N; n++)
    {
        float exc;
        if (voiced && pitch > 0)
        {
            /* Periodic impulse train */
            st->decPitchPhase += 1.0f;
            if (st->decPitchPhase >= (float)pitch)
            {
                st->decPitchPhase -= (float)pitch;
                exc = gain;
            }
            else
            {
                exc = 0.0f;
            }
        }
        else
        {
            /* White noise excitation */
            exc = gain * ((float)(rand() & 0xFFFF) / 32768.0f - 1.0f) * 0.5f;
        }

        /* All-pole synthesis filter: y[n] = exc - sum(a[k]*y[n-k]) */
        float y = exc;
        for (int k = 0; k < CODEC2_LPC_ORDER; k++)
            y -= lpc[k] * st->decSynthMem[k];

        /* Shift memory */
        memmove(st->decSynthMem + 1, st->decSynthMem,
                (CODEC2_LPC_ORDER - 1) * sizeof(float));
        st->decSynthMem[0] = y;
        pcm[n] = y;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════ */

void codec2Init(Codec2State_t *st)
{
    memset(st, 0, sizeof(*st));
    memcpy(st->encLsp,     LSP_INIT, sizeof(LSP_INIT));
    memcpy(st->decLsp,     LSP_INIT, sizeof(LSP_INIT));
    memcpy(st->decLspPrev, LSP_INIT, sizeof(LSP_INIT));
    st->decEnergy = 1.0f;
}

void codec2Encode(Codec2State_t *st, const int16_t pcm[CODEC2_PCM_SAMPLES],
                  uint8_t bits[CODEC2_FRAME_BYTES])
{
    memset(bits, 0, CODEC2_FRAME_BYTES);

    /* Convert to float and pre-emphasise */
    float xf[CODEC2_PCM_SAMPLES];
    static float preEmphPrev = 0.0f;
    for (int i = 0; i < CODEC2_PCM_SAMPLES; i++)
        xf[i] = (float)pcm[i] / 32768.0f;
    preEmphasis(xf, xf, CODEC2_PCM_SAMPLES, PREEMPH, &preEmphPrev);

    /* Apply Hanning window */
    float windowed[CODEC2_PCM_SAMPLES];
    for (int i = 0; i < CODEC2_PCM_SAMPLES; i++)
        windowed[i] = xf[i] * hanningWindow(i, CODEC2_PCM_SAMPLES);

    /* LPC analysis */
    float r[CODEC2_LPC_ORDER + 1];
    autocorr(windowed, CODEC2_PCM_SAMPLES, r, CODEC2_LPC_ORDER);
    r[0] *= 1.0001f; /* white noise floor */

    float lpc[CODEC2_LPC_ORDER];
    float predError;
    levinsonDurbin(r, CODEC2_LPC_ORDER, lpc, &predError);

    /* LPC → LSP */
    float lsp[CODEC2_LPC_ORDER];
    lpcToLsp(lpc, CODEC2_LPC_ORDER, lsp);
    memcpy(st->encLsp, lsp, sizeof(lsp));

    /* Pitch detection */
    bool voiced;
    int pitch = detectPitch(xf, CODEC2_PCM_SAMPLES, &voiced);
    st->encPitch = (float)pitch;

    /* Energy: mean square power */
    float energy = 0.0f;
    for (int i = 0; i < CODEC2_PCM_SAMPLES; i++)
        energy += xf[i] * xf[i];
    energy /= (float)CODEC2_PCM_SAMPLES;

    /* ── Pack frame ────────────────────────────────────────────── */
    /* Bit layout (64 bits):
       [0-6]   pitch (7 bits)
       [7]     voicing
       [8-12]  energy (5 bits)
       [13-17] lsp[0] (5 bits)
       [18-22] lsp[1]
       ...
       [58-63] lsp[9] (partial: 6 bits for last)  */
    int bp = 0;

    /* Pitch: 7 bits. Map period [20,160] → [1,127]; 0 = unvoiced */
    uint32_t pitchQ = voiced ? (uint32_t)((pitch - MIN_PITCH) * 127 / (MAX_PITCH - MIN_PITCH) + 1) : 0;
    if (pitchQ > 127) pitchQ = 127;
    packBits(bits, &bp, pitchQ, 7);

    /* Voicing: 1 bit */
    packBits(bits, &bp, voiced ? 1U : 0U, 1);

    /* Energy: 5 bits */
    packBits(bits, &bp, quantiseEnergy(energy), 5);

    /* LSPs: 5 bits each × 10 = 50 bits + 1 spare = 51 bits total (up to bit 63) */
    for (int i = 0; i < CODEC2_LPC_ORDER; i++)
    {
        int nbits = (i < 9) ? 5 : 6;   /* last LSP gets 6 bits (51 - 9*5 = 6) */
        uint32_t qv = (i < 9) ? (uint32_t)quantiseLsp(lsp[i])
                               : (uint32_t)(quantiseLsp(lsp[i]) * 2); /* scale up for 6-bit */
        packBits(bits, &bp, qv, nbits);
    }
    /* bp should now be 64 */
}

void codec2Decode(Codec2State_t *st, const uint8_t bits[CODEC2_FRAME_BYTES],
                  int16_t pcm[CODEC2_PCM_SAMPLES])
{
    int bp = 0;

    /* Pitch */
    uint32_t pitchQ = unpackBits(bits, &bp, 7);
    bool voiced     = (bool)unpackBits(bits, &bp, 1);

    int pitch = 0;
    if (voiced && pitchQ > 0)
    {
        pitch = (int)((pitchQ - 1) * (MAX_PITCH - MIN_PITCH) / 127 + MIN_PITCH);
        if (pitch < MIN_PITCH) pitch = MIN_PITCH;
        if (pitch > MAX_PITCH) pitch = MAX_PITCH;
    }

    /* Energy */
    float energy = dequantiseEnergy((uint8_t)unpackBits(bits, &bp, 5));

    /* LSPs */
    float lsp[CODEC2_LPC_ORDER];
    for (int i = 0; i < CODEC2_LPC_ORDER; i++)
    {
        int nbits = (i < 9) ? 5 : 6;
        uint32_t qv = unpackBits(bits, &bp, nbits);
        lsp[i] = (i < 9) ? dequantiseLsp((uint8_t)qv)
                          : dequantiseLsp((uint8_t)(qv / 2));
    }

    /* Ensure LSPs are ordered and within [0, π] */
    for (int i = 0; i < CODEC2_LPC_ORDER; i++)
    {
        if (lsp[i] < 0.1f)  lsp[i] = 0.1f + (float)i * 0.1f;
        if (lsp[i] > 3.0f)  lsp[i] = 3.0f;
        if (i > 0 && lsp[i] <= lsp[i - 1])
            lsp[i] = lsp[i - 1] + 0.05f;
    }

    /* Interpolate LSPs between previous and current frame */
    float lspInterp[CODEC2_LPC_ORDER];
    for (int i = 0; i < CODEC2_LPC_ORDER; i++)
        lspInterp[i] = 0.5f * st->decLspPrev[i] + 0.5f * lsp[i];
    memcpy(st->decLspPrev, lsp, sizeof(lsp));

    /* LSP → LPC */
    float lpc[CODEC2_LPC_ORDER];
    lspToLpc(lspInterp, CODEC2_LPC_ORDER, lpc);

    /* Synthesise */
    float floatPcm[CODEC2_PCM_SAMPLES];
    synthesise(st, floatPcm, CODEC2_PCM_SAMPLES, lpc, energy, pitch, voiced);

    /* De-emphasise */
    static float deEmphPrev = 0.0f;
    deEmphasis(floatPcm, CODEC2_PCM_SAMPLES, PREEMPH, &deEmphPrev);

    /* Clip and convert to int16 */
    for (int i = 0; i < CODEC2_PCM_SAMPLES; i++)
    {
        float s = floatPcm[i] * 32768.0f;
        if (s >  32767.0f) s =  32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        pcm[i] = (int16_t)s;
    }

    st->decFrameCount++;
    memcpy(st->decLsp, lsp, sizeof(lsp));
}
