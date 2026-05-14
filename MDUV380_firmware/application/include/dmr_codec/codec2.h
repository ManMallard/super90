/*
 * Codec2 3200 bps voice codec interface.
 *
 * Encodes 160 PCM samples (20 ms @ 8 kHz, 16-bit signed) to 8 bytes (64 bits).
 * Decodes 8 bytes back to 160 PCM samples.
 *
 * This is an LPC-10 based implementation producing a bit-stream compatible
 * with Codec2 mode 3200 as used by M17.  The quantisation tables follow
 * David Rowe's open-source codec2 library (LGPLv2.1).
 *
 * Frame bit layout (64 bits):
 *   Bits  0- 6 : pitch (7 bits, 50–400 Hz, unvoiced = 0)
 *   Bit   7    : voicing decision (1 = voiced)
 *   Bits  8-12 : log energy (5 bits)
 *   Bits 13-63 : LSP quantisation (51 bits split across 10 LSP coefficients)
 */

#ifndef CODEC2_H_
#define CODEC2_H_

#include <stdint.h>
#include <stdbool.h>

#define CODEC2_FRAME_BYTES  8    /* bytes per 20 ms frame */
#define CODEC2_PCM_SAMPLES  160  /* samples per frame (8 kHz, 20 ms) */
#define CODEC2_SAMPLE_RATE  8000
#define CODEC2_LPC_ORDER    10   /* LPC filter order */

typedef struct {
    /* Encoder state */
    float encPrevPCM[CODEC2_PCM_SAMPLES];
    float encLsp[CODEC2_LPC_ORDER];
    float encPitch;
    int   encFrameCount;

    /* Decoder state */
    float decSynthMem[CODEC2_LPC_ORDER]; /* synthesis filter memory */
    float decExcPrev;
    float decPitchPhase;
    float decLsp[CODEC2_LPC_ORDER];
    float decLspPrev[CODEC2_LPC_ORDER];
    float decEnergy;
    int   decFrameCount;
} Codec2State_t;

/* Initialise the codec state */
void codec2Init(Codec2State_t *st);

/* Encode 160 PCM samples → 8 bytes */
void codec2Encode(Codec2State_t *st, const int16_t pcm[CODEC2_PCM_SAMPLES],
                  uint8_t bits[CODEC2_FRAME_BYTES]);

/* Decode 8 bytes → 160 PCM samples */
void codec2Decode(Codec2State_t *st, const uint8_t bits[CODEC2_FRAME_BYTES],
                  int16_t pcm[CODEC2_PCM_SAMPLES]);

#endif /* CODEC2_H_ */
