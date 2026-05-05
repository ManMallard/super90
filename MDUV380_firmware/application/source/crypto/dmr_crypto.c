/*
 * AES-256-CTR wrapper for 27-byte AMBE+2 voice frames.
 *
 * Counter layout (16 bytes):
 *   [0..7]  per-session nonce  (set by dmr_crypto_*_init)
 *   [8..11] big-endian superframe number
 *   [12..15] zero
 *
 * The chip emits voice frames in 27-byte chunks every 60 ms. We treat
 * each chunk as ceil(27/16)=2 AES blocks of keystream and XOR.
 */
#include <string.h>
#include <stdint.h>

#include "crypto/dmr_crypto.h"
#include "crypto/aes.h"
#include "functions/ticks.h"   /* ticksGetMillis() — native on STM32 */

static struct AES_ctx s_txCtx;
static struct AES_ctx s_rxCtx;
static uint8_t        s_txNonce[8];
static uint8_t        s_rxNonce[8];
static uint8_t        s_txActive = 0;
static uint8_t        s_rxActive = 0;

static void make_iv(uint8_t iv[AES_BLOCKLEN], const uint8_t nonce[8], uint32_t superframe)
{
    memcpy(iv, nonce, 8);
    iv[ 8] = (uint8_t)(superframe >> 24);
    iv[ 9] = (uint8_t)(superframe >> 16);
    iv[10] = (uint8_t)(superframe >>  8);
    iv[11] = (uint8_t)(superframe);
    iv[12] = iv[13] = iv[14] = iv[15] = 0;
}

void dmr_crypto_make_nonce(uint8_t nonce[DMR_NONCE_BYTES])
{
    /* Bench-test nonce: ms tick + buffer address + cheap mixing.
     * NOT cryptographically strong. For real deployments, source from
     * the STM32F4's TRNG (RNG peripheral) — left as a polish step. */
    uint32_t a = ticksGetMillis();
    uint32_t b = (uint32_t)(uintptr_t)nonce;
    uint32_t mix1 = a ^ (b * 0x9E3779B9u);
    uint32_t mix2 = (a * 0x85EBCA6Bu) ^ b;
    nonce[0] = (uint8_t)(mix1      ); nonce[1] = (uint8_t)(mix1 >>  8);
    nonce[2] = (uint8_t)(mix1 >> 16); nonce[3] = (uint8_t)(mix1 >> 24);
    nonce[4] = (uint8_t)(mix2      ); nonce[5] = (uint8_t)(mix2 >>  8);
    nonce[6] = (uint8_t)(mix2 >> 16); nonce[7] = (uint8_t)(mix2 >> 24);
}

void dmr_crypto_tx_init(const uint8_t key[32], const uint8_t nonce[8])
{
    uint8_t iv[AES_BLOCKLEN];
    memcpy(s_txNonce, nonce, 8);
    make_iv(iv, s_txNonce, 0);
    AES_init_ctx_iv(&s_txCtx, key, iv);
    s_txActive = 1;
}
void dmr_crypto_tx_clear(void) { s_txActive = 0; memset(&s_txCtx, 0, sizeof(s_txCtx)); }
int  dmr_crypto_tx_active(void) { return s_txActive; }

void dmr_crypto_rx_init(const uint8_t key[32], const uint8_t nonce[8])
{
    uint8_t iv[AES_BLOCKLEN];
    memcpy(s_rxNonce, nonce, 8);
    make_iv(iv, s_rxNonce, 0);
    AES_init_ctx_iv(&s_rxCtx, key, iv);
    s_rxActive = 1;
}
void dmr_crypto_rx_clear(void) { s_rxActive = 0; memset(&s_rxCtx, 0, sizeof(s_rxCtx)); }
int  dmr_crypto_rx_active(void) { return s_rxActive; }

void dmr_crypto_tx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES], uint32_t superframe)
{
    if (!s_txActive) return;
    uint8_t iv[AES_BLOCKLEN];
    make_iv(iv, s_txNonce, superframe);
    memcpy(s_txCtx.Iv, iv, AES_BLOCKLEN);
    AES_CTR_xcrypt_buffer(&s_txCtx, buf, DMR_AMBE_FRAME_BYTES);
}
void dmr_crypto_rx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES], uint32_t superframe)
{
    if (!s_rxActive) return;
    uint8_t iv[AES_BLOCKLEN];
    make_iv(iv, s_rxNonce, superframe);
    memcpy(s_rxCtx.Iv, iv, AES_BLOCKLEN);
    AES_CTR_xcrypt_buffer(&s_rxCtx, buf, DMR_AMBE_FRAME_BYTES);
}
