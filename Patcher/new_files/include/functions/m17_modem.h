/*
 * M17 4FSK software modem — modulator and demodulator.
 *
 * The I2S peripheral runs at 8 kHz (slave, driven by HR-C6000).
 * Symbol rate is 4800 Bd → 8000/4800 = 5/3 samples per symbol.
 * A phase-accumulator drives the TX symbol clock; a Mueller-Müller
 * timing-recovery loop tracks the RX symbol clock.
 *
 * Audio levels:
 *   +3 symbol → +M17_MODEM_LEVEL_HI  (≈ 75 % of full scale)
 *   +1 symbol → +M17_MODEM_LEVEL_LO  (≈ 25 % of full scale)
 *   -1 symbol → -M17_MODEM_LEVEL_LO
 *   -3 symbol → -M17_MODEM_LEVEL_HI
 *
 * The AT1846S FM modulator converts these audio levels to ±2400 Hz /
 * ±800 Hz deviation from the carrier.  Calibration constants can be
 * adjusted if the actual deviation differs from spec.
 */

#ifndef M17_MODEM_H_
#define M17_MODEM_H_

#include <stdint.h>
#include <stdbool.h>
#include "functions/m17.h"

/* ── Modem constants ─────────────────────────────────────────────────── */
#define M17_MODEM_SAMPLE_RATE      8000   /* Hz, locked to I2S clock */
#define M17_MODEM_SYMBOL_RATE      4800   /* Bd */
/* Phase accumulator increment per I2S sample: 4800/8000 = 0.6 */

/* Audio levels (signed 16-bit, scaled to I2S range ±32767) */
#define M17_MODEM_LEVEL_HI         24576  /* ±3 symbol  (75 % FS) */
#define M17_MODEM_LEVEL_LO          8192  /* ±1 symbol  (25 % FS) */

/* TX symbol queue depth: at PTT-down we load preamble (192) + LSF (192) =
 * 384 symbols, and at steady-state we add one stream frame (192) every
 * 40 ms while the modem drains ~192 per 40 ms.  Three frames is enough
 * margin for the startup transient before the first stream frame fires. */
#define M17_MODEM_TX_QUEUE_LEN     (M17_SYMBOLS_PER_FRAME * 3)

/* RX symbol buffer: STREAM state needs exactly M17_SYMBOLS_PER_FRAME
 * symbols (8 sync + 184 payload), LSF state needs 184.  SEARCH state
 * uses a sliding 16-symbol window which is always trimmed back to 8. */
#define M17_MODEM_RX_SYM_BUF_LEN   M17_SYMBOLS_PER_FRAME

/* Sync correlator threshold: max Hamming distance allowed for sync detect */
#define M17_SYNC_THRESHOLD         3

/* ── State types ─────────────────────────────────────────────────────── */

typedef enum {
    M17_MODEM_IDLE = 0,
    M17_MODEM_RX_SEARCH,   /* hunting for preamble / sync word */
    M17_MODEM_RX_LSF,      /* receiving LSF burst */
    M17_MODEM_RX_STREAM,   /* receiving stream frames */
    M17_MODEM_TX_PREAMBLE,
    M17_MODEM_TX_LSF,
    M17_MODEM_TX_STREAM,
    M17_MODEM_TX_EOT,
} M17ModemState_t;

typedef struct {
    /* TX state */
    float    txPhase;                                /* 0.0 – 1.0 symbol clock phase */
    int8_t   txSymbolQueue[M17_MODEM_TX_QUEUE_LEN];  /* symbol output FIFO */
    int      txQueueHead;
    int      txQueueTail;
    int      txQueueCount;
    int      txSamplesInSymbol;                      /* samples emitted for current sym */

    /* RX state — samples are processed directly from the I2S input slice; no
     * separate sample buffer is kept in this context. */
    M17ModemState_t state;
    float    rxPhase;                                /* symbol timing phase */
    float    rxTED;                                  /* timing error */
    int8_t   rxSymbols[M17_MODEM_RX_SYM_BUF_LEN];    /* decoded symbols FIFO */
    int      rxSymCount;
    int      rxFrameSymCount;                        /* symbols collected in current frame */

    /* LICH re-assembly */
    uint8_t  lichPartial[M17_LSF_SIZE];
    uint8_t  lichChunkSeen;                          /* bitmask of received chunks */
    bool     lsfValid;
    M17Lsf_t currentLsf;
    uint16_t frameNumber;
} M17ModemCtx_t;

/* ── Public API ──────────────────────────────────────────────────────── */

/* Initialise (or reset) the modem context */
void m17ModemInit(M17ModemCtx_t *ctx);

/* ── TX path ─────────────────────────────────────────────────────────── */

/* Load a complete frame of symbols into the TX queue */
void m17ModemTxLoad(M17ModemCtx_t *ctx, const int8_t symbols[M17_SYMBOLS_PER_FRAME]);

/* Called from soundRefillData() for each I2S TX buffer slice.
   Writes `count` 16-bit signed samples into `out`.
   Returns number of samples written. */
int  m17ModemTxFill(M17ModemCtx_t *ctx, int16_t *out, int count);

/* True when the TX queue is empty (all symbols transmitted) */
bool m17ModemTxDone(const M17ModemCtx_t *ctx);

/* ── RX path ─────────────────────────────────────────────────────────── */

/* Feed new I2S RX samples into the demodulator.
   Returns true if a complete M17 stream frame was decoded; the decoded
   frame is written to *frame_out.  Call repeatedly until it returns false. */
bool m17ModemRxFeed(M17ModemCtx_t *ctx, const int16_t *samples, int count,
                    M17StreamFrame_t *frame_out, M17Lsf_t *lsf_out);

/* Reset RX state machine back to SEARCH */
void m17ModemRxReset(M17ModemCtx_t *ctx);

#endif /* M17_MODEM_H_ */
