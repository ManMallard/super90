/*
 * M17 4FSK software modem.
 *
 * TX: symbol queue → phase-accumulator output → 16-bit I2S samples.
 * RX: I2S samples → DC-block → normalise → Mueller-Müller TED →
 *     symbol slicer → sync correlator → frame assembler.
 */

#include "functions/m17_modem.h"
#include <string.h>
#include <stdlib.h>

/* Phase accumulator increment: symbol_rate / sample_rate = 4800 / 8000 */
#define TX_PHASE_INC   (4800.0f / 8000.0f)   /* 0.6 */

/* ── RRC filter (approximate) ─────────────────────────────────────────
 * A simple 5-tap low-pass matched filter at Fc ≈ 2400 Hz / 8000 Hz = 0.3.
 * For a proper RRC we would need more taps, but this reduces ISI
 * significantly at the low oversampling ratio we have. */
#define RRC_TAPS  5
static const float RRC_COEFFS[RRC_TAPS] = { 0.12f, 0.23f, 0.30f, 0.23f, 0.12f };

/* Apply RRC to a scalar sample; rrcState[0..4] is caller-maintained */
static float rrcFilter(float x, float state[RRC_TAPS])
{
    float y = 0.0f;
    for (int i = RRC_TAPS - 1; i > 0; i--) state[i] = state[i - 1];
    state[0] = x;
    for (int i = 0; i < RRC_TAPS; i++) y += RRC_COEFFS[i] * state[i];
    return y;
}

/* ── DC blocker (IIR, α = 0.995) ────────────────────────────────────── */
static float dcBlock(float x, float *xPrev, float *yPrev)
{
    float y = x - *xPrev + 0.995f * (*yPrev);
    *xPrev = x;
    *yPrev = y;
    return y;
}

/* ── Automatic level normaliser (running max) ─────────────────────── */
static float normalise(float x, float *peak)
{
    float ax = x < 0 ? -x : x;
    if (ax > *peak) *peak = ax;
    if (*peak < 1.0f) return x;
    /* Slow decay so peak doesn't drift too fast */
    *peak *= 0.9999f;
    return x / *peak;
}

/* ── Symbol slicer ────────────────────────────────────────────────── */
static int8_t slice4FSK(float x)
{
    /* Thresholds at ±2 (normalised ±3/±1 with unit spacing) */
    if (x >  2.0f) return  3;
    if (x >  0.0f) return  1;
    if (x > -2.0f) return -1;
    return -3;
}

/* ── Sync correlator ──────────────────────────────────────────────── */
/* Convert a 16-bit sync word to 8 symbol values */
static void syncWordToSymbols(uint16_t sw, int8_t sym[8])
{
    for (int i = 7; i >= 0; i--)
    {
        uint8_t dibit = (uint8_t)((sw >> (i * 2)) & 0x3);
        sym[7 - i] = m17DibitToSymbol(dibit);
    }
}

/* Hamming-distance-like metric between received and expected symbols */
static int syncCorrelate(const int8_t *rx, const int8_t *ref, int len)
{
    int err = 0;
    for (int i = 0; i < len; i++)
        if (rx[i] != ref[i]) err++;
    return err;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Public functions
 * ══════════════════════════════════════════════════════════════════════ */

void m17ModemInit(M17ModemCtx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = M17_MODEM_RX_SEARCH;
}

/* ── TX ──────────────────────────────────────────────────────────── */

void m17ModemTxLoad(M17ModemCtx_t *ctx, const int8_t symbols[M17_SYMBOLS_PER_FRAME])
{
    for (int i = 0; i < M17_SYMBOLS_PER_FRAME; i++)
    {
        ctx->txSymbolQueue[ctx->txQueueTail] = symbols[i];
        ctx->txQueueTail = (ctx->txQueueTail + 1) % M17_MODEM_TX_QUEUE_LEN;
        ctx->txQueueCount++;
    }
}

int m17ModemTxFill(M17ModemCtx_t *ctx, int16_t *out, int count)
{
    int written = 0;
    for (int i = 0; i < count; i++)
    {
        if (ctx->txQueueCount == 0)
        {
            out[i] = 0;
            continue;
        }
        int8_t sym = ctx->txSymbolQueue[ctx->txQueueHead];

        /* Map symbol ±3/±1 to audio level */
        int16_t level;
        switch (sym)
        {
            case  3: level =  M17_MODEM_LEVEL_HI; break;
            case  1: level =  M17_MODEM_LEVEL_LO; break;
            case -1: level = -M17_MODEM_LEVEL_LO; break;
            default: level = -M17_MODEM_LEVEL_HI; break;
        }
        out[i] = level;
        written++;

        /* Advance symbol clock via phase accumulator */
        ctx->txPhase += TX_PHASE_INC;
        if (ctx->txPhase >= 1.0f)
        {
            ctx->txPhase -= 1.0f;
            ctx->txQueueHead = (ctx->txQueueHead + 1) % M17_MODEM_TX_QUEUE_LEN;
            ctx->txQueueCount--;
        }
    }
    return written;
}

bool m17ModemTxDone(const M17ModemCtx_t *ctx)
{
    return ctx->txQueueCount == 0;
}

/* ── RX ──────────────────────────────────────────────────────────── */

void m17ModemRxReset(M17ModemCtx_t *ctx)
{
    ctx->state         = M17_MODEM_RX_SEARCH;
    ctx->rxPhase       = 0.0f;
    ctx->rxTED         = 0.0f;
    ctx->rxSymCount    = 0;
    ctx->rxFrameSymCount = 0;
    ctx->lichChunkSeen = 0;
    ctx->lsfValid      = false;
    ctx->frameNumber   = 0;
}

/* Per-instance RX filter state (static so it persists across calls) */
typedef struct {
    float rrcState[RRC_TAPS];
    float dcXprev, dcYprev;
    float peak;
    float mmPrev;   /* previous sample for M&M TED */
    float mmLast;   /* last sliced symbol value */
} RxFilterState_t;

static RxFilterState_t s_rxFilt;

bool m17ModemRxFeed(M17ModemCtx_t *ctx, const int16_t *samples, int count,
                    M17StreamFrame_t *frame_out, M17Lsf_t *lsf_out)
{
    static int8_t streamRef[8], eotRef[8], lsfRef[8];
    static bool refsBuilt = false;
    if (!refsBuilt)
    {
        syncWordToSymbols(M17_SYNC_STREAM, streamRef);
        syncWordToSymbols(M17_SYNC_EOT,    eotRef);
        syncWordToSymbols(M17_SYNC_LSF,    lsfRef);
        refsBuilt = true;
    }

    bool frameDecoded = false;

    for (int i = 0; i < count && !frameDecoded; i++)
    {
        /* ── Pre-processing ─────────────────────────────────────── */
        float x = (float)samples[i] * (1.0f / 32768.0f);
        x = dcBlock(x, &s_rxFilt.dcXprev, &s_rxFilt.dcYprev);
        x = rrcFilter(x, s_rxFilt.rrcState);
        x = normalise(x, &s_rxFilt.peak);

        /* ── Symbol timing (Mueller-Müller TED) ─────────────────── */
        ctx->rxPhase += TX_PHASE_INC;  /* advance at symbol rate */

        if (ctx->rxPhase >= 1.0f)
        {
            ctx->rxPhase -= 1.0f;

            /* M&M timing error: TED = sign(prev_sample) * current - sign(current) * prev */
            float slicedNow  = (float)slice4FSK(x * 3.0f);  /* scale: ±1 → ±3 */
            float slicedPrev = s_rxFilt.mmLast;
            float ted = (slicedPrev > 0 ? 1.0f : -1.0f) * x
                      - (slicedNow  > 0 ? 1.0f : -1.0f) * s_rxFilt.mmPrev;
            ctx->rxTED  = 0.01f * ted + 0.99f * ctx->rxTED;
            ctx->rxPhase -= ctx->rxTED * 0.02f;  /* feedback: adjust clock */

            s_rxFilt.mmPrev = x;
            s_rxFilt.mmLast = slicedNow;

            /* ── Sliced symbol ──────────────────────────────────── */
            int8_t sym = slice4FSK(x * 3.0f);

            /* Store in rolling symbol buffer */
            if (ctx->rxSymCount < (int)(sizeof(ctx->rxSymbols)))
                ctx->rxSymbols[ctx->rxSymCount++] = sym;

            /* ── State machine ──────────────────────────────────── */
            switch (ctx->state)
            {
            case M17_MODEM_RX_SEARCH:
                /* Look for any sync word in the last 8 received symbols */
                if (ctx->rxSymCount >= 8)
                {
                    const int8_t *tail = ctx->rxSymbols + ctx->rxSymCount - 8;
                    int lsfErr    = syncCorrelate(tail, lsfRef,    8);
                    int streamErr = syncCorrelate(tail, streamRef, 8);
                    int eotErr    = syncCorrelate(tail, eotRef,    8);

                    if (lsfErr <= M17_SYNC_THRESHOLD)
                    {
                        /* Found LSF sync: next 184 symbols are encoded LSF */
                        ctx->state = M17_MODEM_RX_LSF;
                        ctx->rxFrameSymCount = 0;
                        ctx->rxSymCount = 0;
                    }
                    else if (streamErr <= M17_SYNC_THRESHOLD
                             || eotErr <= M17_SYNC_THRESHOLD)
                    {
                        ctx->state = M17_MODEM_RX_STREAM;
                        ctx->rxFrameSymCount = 8;  /* sync already counted */
                        /* Copy the 8 sync symbols to start of frame buffer */
                        memmove(ctx->rxSymbols, tail, 8);
                        ctx->rxSymCount = 8;
                    }
                }
                /* Keep buffer bounded */
                if (ctx->rxSymCount > 16)
                {
                    memmove(ctx->rxSymbols, ctx->rxSymbols + ctx->rxSymCount - 8, 8);
                    ctx->rxSymCount = 8;
                }
                break;

            case M17_MODEM_RX_LSF:
                ctx->rxFrameSymCount++;
                if (ctx->rxFrameSymCount >= M17_LSF_PAYLOAD_SYMBOLS)
                {
                    /* We have 184 payload symbols for the LSF.
                       Decode the LSF from the stored symbols.
                       The first 8 symbols (sync) were consumed finding the sync. */
                    uint8_t lsfBits[M17_LSF_ENCODED_BITS];
                    int8_t *syms = ctx->rxSymbols;
                    for (int s = 0; s < M17_LSF_PAYLOAD_SYMBOLS; s++)
                    {
                        uint8_t d = m17SymbolToDibit(syms[s]);
                        lsfBits[2 * s]     = (d >> 1) & 1;
                        lsfBits[2 * s + 1] = d & 1;
                    }
                    if (m17LsfDecode(lsfBits, &ctx->currentLsf))
                    {
                        ctx->lsfValid = true;
                        if (lsf_out) *lsf_out = ctx->currentLsf;
                        /* Build a raw LSF byte buffer for LICH */
                        uint8_t rawLsf[M17_LSF_SIZE];
                        memcpy(rawLsf,      ctx->currentLsf.dst,  6);
                        memcpy(rawLsf + 6,  ctx->currentLsf.src,  6);
                        rawLsf[12] = (uint8_t)(ctx->currentLsf.type >> 8);
                        rawLsf[13] = (uint8_t)(ctx->currentLsf.type & 0xFF);
                        memcpy(rawLsf + 14, ctx->currentLsf.meta, 14);
                        rawLsf[28] = (uint8_t)(ctx->currentLsf.crc >> 8);
                        rawLsf[29] = (uint8_t)(ctx->currentLsf.crc & 0xFF);
                        memcpy(ctx->lichPartial, rawLsf, M17_LSF_SIZE);
                        ctx->lichChunkSeen = 0x3F;  /* all chunks known */
                    }
                    ctx->state = M17_MODEM_RX_STREAM;
                    ctx->rxSymCount = 0;
                    ctx->rxFrameSymCount = 0;
                }
                break;

            case M17_MODEM_RX_STREAM:
                ctx->rxFrameSymCount++;
                if (ctx->rxFrameSymCount >= M17_SYMBOLS_PER_FRAME)
                {
                    /* Full 192-symbol frame available */
                    M17StreamFrame_t frame;
                    if (m17StreamFrameDecode(ctx->rxSymbols, &frame))
                    {
                        /* Update LICH partial LSF */
                        for (int ci = 0; ci < M17_LICH_CHUNKS; ci++)
                        {
                            if (!(ctx->lichChunkSeen & (1 << ci)))
                            {
                                bool done;
                                m17LichDecodeChunk(frame.lich, (uint8_t)ci,
                                                   ctx->lichPartial, &done);
                                if (done)
                                {
                                    /* Validate assembled LSF */
                                    uint16_t crc = m17Crc(ctx->lichPartial, 28);
                                    if (crc == ((uint16_t)ctx->lichPartial[28] << 8
                                                | ctx->lichPartial[29]))
                                    {
                                        ctx->lichChunkSeen = 0x3F;
                                        ctx->lsfValid = true;
                                    }
                                }
                                else
                                {
                                    ctx->lichChunkSeen |= (uint8_t)(1 << ci);
                                }
                                break;
                            }
                        }

                        *frame_out = frame;
                        if (lsf_out && ctx->lsfValid)
                        {
                            /* Re-populate lsf_out from lichPartial */
                            memcpy(lsf_out->dst,  ctx->lichPartial,      6);
                            memcpy(lsf_out->src,  ctx->lichPartial + 6,  6);
                            lsf_out->type = ((uint16_t)ctx->lichPartial[12] << 8)
                                            | ctx->lichPartial[13];
                            memcpy(lsf_out->meta, ctx->lichPartial + 14, 14);
                            lsf_out->crc  = ((uint16_t)ctx->lichPartial[28] << 8)
                                            | ctx->lichPartial[29];
                        }
                        frameDecoded = true;

                        if (frame.isEot)
                            m17ModemRxReset(ctx);
                    }
                    else
                    {
                        /* Sync lost — go back to search */
                        m17ModemRxReset(ctx);
                    }
                    ctx->rxSymCount = 0;
                    ctx->rxFrameSymCount = 0;
                }
                break;

            default:
                break;
            }
        }
    }

    return frameDecoded;
}
