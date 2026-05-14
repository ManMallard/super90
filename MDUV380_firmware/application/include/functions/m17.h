/*
 * M17 protocol layer — frame definitions, Golay FEC, CRC-16, callsign encoding,
 * LSF and stream-frame encode/decode.
 *
 * Physical constants from M17 Specification v1.0 (m17project.org).
 * Convolutional polynomials: G1=0x19, G2=0x17 (K=5, rate 1/2).
 * Golay(24,12) generator: 0xC75.
 * CRC-16: poly=0x5935, init=0xFFFF.
 */

#ifndef M17_H_
#define M17_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Frame geometry ─────────────────────────────────────────────────── */
#define M17_SYMBOLS_PER_FRAME     192   /* symbols at 4800 Bd → 40 ms */
#define M17_BITS_PER_FRAME        384   /* 2 bits per symbol */

/* ── Sync words (16 bits, transmitted MSB-first as dibit pairs) ──── */
#define M17_SYNC_LSF              0x55F7U
#define M17_SYNC_STREAM           0xFF5DU
#define M17_SYNC_PACKET           0x75FFU
#define M17_SYNC_EOT              0x555DU
#define M17_SYNC_BERT             0xDF55U
#define M17_SYNC_SYMBOLS          8     /* 16 bits ÷ 2 bits/symbol */

/* ── LSF raw sizes ──────────────────────────────────────────────────── */
#define M17_LSF_SIZE              30    /* bytes: DST(6)+SRC(6)+TYPE(2)+META(14)+CRC(2) */
#define M17_LSF_BITS              240   /* = 30 × 8 */
/* After K=5 rate-1/2 conv encode (240+4 flush bits → 488 coded bits)
   then puncture pattern P1 → 368 coded bits.
   Transmitted: 8 sync symbols + 184 payload symbols = 192 total.    */
#define M17_LSF_ENCODED_BITS      368
#define M17_LSF_PAYLOAD_SYMBOLS   184

/* ── Stream frame sizes ─────────────────────────────────────────────── */
#define M17_LICH_BITS             48    /* 6 bytes per stream frame (Golay encoded) */
#define M17_STREAM_PAYLOAD_BITS   128   /* 16 bytes voice/data payload */
#define M17_STREAM_PAYLOAD_BYTES  16
/* Stream frame after encoding:
   8 sync + 24 LICH symbols + 160 payload symbols = 192 total symbols. */
#define M17_LICH_SYMBOLS          24
#define M17_STREAM_DATA_SYMBOLS   160

/* ── LICH: LSF is sent in chunks over 6 consecutive stream frames ─── */
#define M17_LICH_CHUNKS           6
#define M17_LICH_CHUNK_BYTES      5    /* 40 raw LSF bits per LICH chunk */

/* ── Codec2 3200 sizes ──────────────────────────────────────────────── */
#define M17_C2_FRAME_BYTES        8    /* 64 bits per 20 ms Codec2 3200 frame */
#define M17_C2_FRAMES_PER_M17     2    /* two Codec2 frames fill one stream payload */
#define M17_C2_PCM_SAMPLES        160  /* samples per 20 ms @ 8 kHz */

/* ── TYPE field bits ────────────────────────────────────────────────── */
#define M17_TYPE_STREAM           0x0002U  /* bits[1:0] = 10: stream mode */
#define M17_TYPE_VOICE            0x0004U  /* bits[3:2] = 01: voice only */
#define M17_TYPE_CODEC2_3200      0x0000U  /* bits[5:4] = 00: Codec2 3200 */
#define M17_TYPE_ENCRYPTED_NONE   0x0000U  /* bits[7:6] = 00: no encryption */
#define M17_TYPE_DEFAULT          (M17_TYPE_STREAM | M17_TYPE_VOICE | M17_TYPE_CODEC2_3200)

/* ── Callsign ───────────────────────────────────────────────────────── */
#define M17_CALLSIGN_BYTES        6    /* 48-bit base40 encoded */
#define M17_CALLSIGN_MAX_CHARS    9    /* max 9 alphanumeric + symbols */
#define M17_BROADCAST_ADDR        0xFFFFFFFFFFFFULL

/* ── Preamble ───────────────────────────────────────────────────────── */
#define M17_PREAMBLE_SYMBOLS      192  /* 40 ms of alternating +3 / −3 */

/* ─────────────────────────────────────────────────────────────────────
 *  Data types
 * ───────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t dst[M17_CALLSIGN_BYTES];
    uint8_t src[M17_CALLSIGN_BYTES];
    uint16_t type;
    uint8_t meta[14];
    uint16_t crc;               /* filled by m17LsfEncode() */
} M17Lsf_t;

typedef struct {
    uint8_t lich[6];            /* Golay-encoded LICH chunk for this frame */
    uint8_t payload[M17_STREAM_PAYLOAD_BYTES];
    uint16_t fn;                /* frame number (0-based, wraps at 0x8000) */
    bool isEot;                 /* true for end-of-transmission frame */
} M17StreamFrame_t;

/* Output of the physical-layer encoder: 192 4FSK symbols (int8, range ±3) */
typedef struct {
    int8_t symbols[M17_SYMBOLS_PER_FRAME];
} M17PhysFrame_t;

/* ─────────────────────────────────────────────────────────────────────
 *  Public API
 * ───────────────────────────────────────────────────────────────────── */

/* Callsign encode/decode (base40, 6 bytes) */
void     m17CallsignEncode(const char *call, uint8_t out[6]);
void     m17CallsignDecode(const uint8_t in[6], char out[10]);

/* CRC-16 (poly 0x5935, init 0xFFFF) */
uint16_t m17Crc(const uint8_t *data, size_t len);

/* Golay(24,12) — encode 12 bits to 24 bits, decode 24 bits to 12 bits */
uint32_t m17GolayEncode(uint16_t data12);
int      m17GolayDecode(uint32_t rx24, uint16_t *data12);  /* returns corrected-error count, -1 on failure */

/* LSF encode: fills lsf->crc and writes 368 convolutionally-coded bits into out[46] */
void     m17LsfEncode(M17Lsf_t *lsf, uint8_t out_bits[M17_LSF_ENCODED_BITS]);

/* LSF decode: runs Viterbi on 368 coded bits, validates CRC, fills *lsf */
bool     m17LsfDecode(const uint8_t coded_bits[M17_LSF_ENCODED_BITS], M17Lsf_t *lsf);

/* LICH: get the n-th chunk (0-5) from a raw LSF, Golay-encode it to 6 bytes */
void     m17LichBuildChunk(const uint8_t raw_lsf[M17_LSF_SIZE], uint8_t chunk_idx, uint8_t lich_out[6]);

/* LICH decode: collect all 6 chunks, re-assemble the 30-byte LSF */
bool     m17LichDecodeChunk(const uint8_t lich_in[6], uint8_t chunk_idx,
                             uint8_t partial_lsf[M17_LSF_SIZE], bool *lsf_complete);

/* Stream-frame encode to 192 raw symbols */
void     m17StreamFrameEncode(const M17StreamFrame_t *frame, M17PhysFrame_t *out);

/* Stream-frame decode from 192 raw symbols (±3,±1 integer levels) */
bool     m17StreamFrameDecode(const int8_t symbols[M17_SYMBOLS_PER_FRAME],
                              M17StreamFrame_t *frame_out);

/* Preamble: fill buf[192] with alternating +3/−3 */
void     m17PreambleFill(int8_t symbols[M17_PREAMBLE_SYMBOLS]);

/* EOT: fill one 192-symbol frame with the end-of-transmission pattern */
void     m17EotFill(int8_t symbols[M17_SYMBOLS_PER_FRAME]);

/* Dibit ↔ symbol mapping (Gray-coded per M17 spec) */
static inline int8_t m17DibitToSymbol(uint8_t dibit)
{
    /* 00→+1, 01→+3, 10→-1, 11→-3 */
    static const int8_t tbl[4] = { 1, 3, -1, -3 };
    return tbl[dibit & 0x3];
}

static inline uint8_t m17SymbolToDibit(int8_t sym)
{
    if (sym >= 2)  return 1;   /* +3 → 01 */
    if (sym >= 0)  return 0;   /* +1 → 00 */
    if (sym >= -2) return 2;   /* -1 → 10 */
    return 3;                  /* -3 → 11 */
}

#endif /* M17_H_ */
