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
#include "crypto/key_storage.h"
#include "crypto/sha256.h"
#include "functions/ticks.h"   /* ticksGetMillis() — fallback nonce mixer */
#include "functions/codeplug.h" /* CodeplugChannel_t */
#include "functions/trx.h"      /* RADIO_MODE_DIGITAL */
#include "stm32f4xx.h"         /* RNG, RCC register definitions */

extern CodeplugChannel_t *currentChannelData;

/* AES patch: engagement-cache state at file scope so the LC-steal helpers
 * (mode A) can read s_lastIdx and s_lastFlags without re-walking the
 * channel/keystore on every TX/RX frame. */
static const CodeplugChannel_t *s_lastChan = NULL;
static uint8_t  s_lastChMode = 0xFF;
static uint8_t  s_lastIdx    = 0xFF;
static uint8_t  s_lastFlags  = 0xFF;
static uint32_t s_lastKeyHash32 = 0;

static struct AES_ctx s_txCtx;
static struct AES_ctx s_rxCtx;
static uint8_t        s_txNonce[8];
static uint8_t        s_rxNonce[8];
static uint8_t        s_txActive = 0;
static uint8_t        s_rxActive = 0;
/* AES patch: counters owned by dmr_crypto, reset on every _init().
 * Previously these lived as `static` locals inside the HR-C6000 SPI taps
 * and never reset, which meant PTT-mode re-init didn't actually restart
 * the keystream. Hoisting them here makes the counter lifetime match the
 * crypto context lifetime. */
static uint32_t       s_txSuperframe = 0;
static uint32_t       s_rxSuperframe = 0;
static uint8_t        s_rngReady = 0;

static void rng_init_once(void)
{
    if (s_rngReady) return;
    /* Enable clock to RNG peripheral (AHB2). */
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
    /* tiny barrier — let the clock-gating bus settle before touching RNG */
    (void)RCC->AHB2ENR;
    /* Enable the RNG itself. */
    RNG->CR |= RNG_CR_RNGEN;
    s_rngReady = 1;
}

static uint32_t rng_word(void)
{
    rng_init_once();
    /* Wait for data-ready (DRDY) and check no error flags. If the seed
     * faults persist, fall back to the ms-tick mixer rather than hang. */
    uint32_t spins = 100000;
    while ((RNG->SR & (RNG_SR_DRDY | RNG_SR_CECS | RNG_SR_SECS)) != RNG_SR_DRDY) {
        if (--spins == 0) {
            uint32_t a = ticksGetMillis();
            uint32_t b = (uint32_t)(uintptr_t)&spins;
            return (a * 0x9E3779B9u) ^ b;
        }
    }
    return RNG->DR;
}

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
    /* STM32F405 hardware TRNG. ~40 cycles per word at 168 MHz when ready,
     * so this returns sub-microsecond after the peripheral is warmed up. */
    uint32_t lo = rng_word();
    uint32_t hi = rng_word();
    nonce[0] = (uint8_t)(lo      ); nonce[1] = (uint8_t)(lo >>  8);
    nonce[2] = (uint8_t)(lo >> 16); nonce[3] = (uint8_t)(lo >> 24);
    nonce[4] = (uint8_t)(hi      ); nonce[5] = (uint8_t)(hi >>  8);
    nonce[6] = (uint8_t)(hi >> 16); nonce[7] = (uint8_t)(hi >> 24);
}

void dmr_crypto_tx_init(const uint8_t key[32], const uint8_t nonce[8])
{
    uint8_t iv[AES_BLOCKLEN];
    memcpy(s_txNonce, nonce, 8);
    make_iv(iv, s_txNonce, 0);
    AES_init_ctx_iv(&s_txCtx, key, iv);
    s_txSuperframe = 0;   /* AES patch: reset counter on every PTT/engage */
    s_txActive = 1;
}
void dmr_crypto_tx_clear(void) { s_txActive = 0; s_txSuperframe = 0; memset(&s_txCtx, 0, sizeof(s_txCtx)); }
int  dmr_crypto_tx_active(void) { return s_txActive; }

void dmr_crypto_rx_init(const uint8_t key[32], const uint8_t nonce[8])
{
    uint8_t iv[AES_BLOCKLEN];
    memcpy(s_rxNonce, nonce, 8);
    make_iv(iv, s_rxNonce, 0);
    AES_init_ctx_iv(&s_rxCtx, key, iv);
    s_rxSuperframe = 0;   /* AES patch: reset counter on every call/engage */
    s_rxActive = 1;
}
void dmr_crypto_rx_clear(void) { s_rxActive = 0; s_rxSuperframe = 0; memset(&s_rxCtx, 0, sizeof(s_rxCtx)); }
int  dmr_crypto_rx_active(void) { return s_rxActive; }

/* AES patch: superframe counter ceiling. AES-CTR mandates that the
 * (key, nonce, counter) triple never repeat; once the counter is exhausted
 * we MUST tear down rather than wrap. At ~17 voice frames/sec this gives
 * ~7 days of continuous TX/RX before the safety trip, far longer than any
 * realistic PTT. PTT mode resets the counter per push so the limit is
 * effectively unreachable; Det mode is the one that benefits from this. */
#define DMR_CRYPTO_SUPERFRAME_CEILING 10000000u

void dmr_crypto_tx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES])
{
    if (!s_txActive) return;
    if (s_txSuperframe >= DMR_CRYPTO_SUPERFRAME_CEILING) {
        /* Tear down — caller will see s_txActive == 0 next frame and
         * the SPI tap will skip encryption (audio plays plaintext). */
        s_txActive = 0;
        return;
    }
    uint8_t iv[AES_BLOCKLEN];
    make_iv(iv, s_txNonce, s_txSuperframe);
    memcpy(s_txCtx.Iv, iv, AES_BLOCKLEN);
    AES_CTR_xcrypt_buffer(&s_txCtx, buf, DMR_AMBE_FRAME_BYTES);
    s_txSuperframe++;
}
void dmr_crypto_rx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES])
{
    if (!s_rxActive) return;
    if (s_rxSuperframe >= DMR_CRYPTO_SUPERFRAME_CEILING) {
        s_rxActive = 0;
        return;
    }
    uint8_t iv[AES_BLOCKLEN];
    make_iv(iv, s_rxNonce, s_rxSuperframe);
    memcpy(s_rxCtx.Iv, iv, AES_BLOCKLEN);
    AES_CTR_xcrypt_buffer(&s_rxCtx, buf, DMR_AMBE_FRAME_BYTES);
    s_rxSuperframe++;
}

/* ---------------------------------------------------------------------------
 * High-level "engage encryption based on current channel state".
 *
 * Called from trxActivateDMRTx() each time DMR TX starts, and from a
 * channel-load hook for RX. Behaviour:
 *
 *   - currentChannelData NULL or analog mode -> tear down both contexts
 *   - encKeyIndex == 0                       -> tear down both contexts
 *   - encKeyIndex valid, slot populated      -> init TX and RX with the
 *                                               slot key and a deterministic
 *                                               nonce (= SHA256(key) high 8B)
 *   - encKeyIndex valid, slot EMPTY          -> tear down (so taps skip);
 *                                               UI shows the warning banner
 *
 * NONCE WARNING: the deterministic nonce design means same key+same data
 * produces the same ciphertext on every PTT, which is cryptographically
 * weak (CTR-mode nonce reuse exposes XOR of plaintexts). Acceptable for
 * bench testing because it lets two radios with the same passphrase
 * decrypt each other without needing to exchange a per-session nonce.
 * Replace with per-PTT random nonce + in-band transmission for production.
 * ------------------------------------------------------------------------- */
static void derive_deterministic_nonce(const uint8_t key[KEY_BYTES], uint8_t nonce[8])
{
    /* SHA-256 of "OpenGD77-NONCE-v1" || key, take first 8 bytes. */
    static const uint8_t prefix[] = "OpenGD77-NONCE-v1";
    SHA256_CTX ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, prefix, sizeof(prefix) - 1);
    sha256_update(&ctx, key, KEY_BYTES);
    sha256_final(&ctx, digest);
    memcpy(nonce, digest, 8);
}

void aes_patch_engage_for_current_channel(void)
{
    /* AES patch: cache state lives at file scope (declared above) so the
     * LC-steal helpers can read it. We only re-init when the
     * (channel-pointer, mode, encKeyIndex, slot-flags) tuple actually
     * changed since last engagement. */

    /* Tear-down conditions. */
    if (currentChannelData == NULL
        || currentChannelData->chMode != RADIO_MODE_DIGITAL)
    {
        if (s_lastChan != NULL || s_lastIdx != 0) {
            dmr_crypto_tx_clear();
            dmr_crypto_rx_clear();
            s_lastChan = currentChannelData;
            s_lastChMode = currentChannelData ? currentChannelData->chMode : 0xFF;
            s_lastIdx = 0;
            s_lastFlags = 0;
            s_lastKeyHash32 = 0;
        }
        return;
    }

    uint8_t idx = currentChannelData->encKeyIndex;
    if (idx == 0 || idx > KEY_SLOT_COUNT)
    {
        if (s_lastIdx != 0) {
            dmr_crypto_tx_clear();
            dmr_crypto_rx_clear();
        }
        s_lastChan = currentChannelData;
        s_lastChMode = currentChannelData->chMode;
        s_lastIdx = 0;
        s_lastFlags = 0;
        s_lastKeyHash32 = 0;
        return;
    }

    const KeySlot_t *ks = keystore_get(idx);
    if (ks == NULL || !(ks->flags & KEY_FLAG_SET))
    {
        /* Slot empty - tear down so taps skip and audio transmits in clear. */
        if (s_lastIdx != idx || s_lastFlags != 0) {
            dmr_crypto_tx_clear();
            dmr_crypto_rx_clear();
        }
        s_lastChan = currentChannelData;
        s_lastChMode = currentChannelData->chMode;
        s_lastIdx = idx;
        s_lastFlags = 0;
        s_lastKeyHash32 = 0;
        return;
    }

    /* Compute a tiny hash of the key to detect re-entry of the same slot
     * with a different passphrase (without keeping a full copy of the key). */
    uint32_t keyHash32 = 0;
    for (int i = 0; i < KEY_BYTES; i++) {
        keyHash32 = (keyHash32 * 0x01000193u) ^ ks->key[i];
    }

    /* Early-exit if nothing meaningful changed since last call. */
    if (s_lastChan == currentChannelData
        && s_lastChMode == currentChannelData->chMode
        && s_lastIdx == idx
        && s_lastFlags == ks->flags
        && s_lastKeyHash32 == keyHash32)
    {
        return;
    }

    /* AES patch: dispatch by per-slot nonce mode.
     *   DETERMINISTIC: nonce derived from key, same every PTT.
     *   A_LC_STEAL:    init with deterministic nonce here, but the per-PTT
     *                  random nonce will overwrite it via
     *                  aes_patch_lc_steal_apply_tx() at PTT-down and
     *                  aes_patch_lc_steal_check_and_apply_rx() on each
     *                  incoming LC frame. The deterministic init ensures
     *                  s_txActive/s_rxActive are set so the SPI taps fire. */
    uint8_t nonce[8];
    derive_deterministic_nonce(ks->key, nonce);
    dmr_crypto_tx_init(ks->key, nonce);
    dmr_crypto_rx_init(ks->key, nonce);

    s_lastChan = currentChannelData;
    s_lastChMode = currentChannelData->chMode;
    s_lastIdx = idx;
    s_lastFlags = ks->flags;
    s_lastKeyHash32 = keyHash32;
}

/* ===========================================================================
 * Option A — LC-steal per-PTT nonce wire format
 *
 *   LC byte index:   0   1   2   3-5      6-8      9   10  11
 *   Plain DMR:       FLG 00  00  destID   srcID    00  00  00
 *   Encrypted (A):   FLG N0  AE  destID   srcID    N1  N2  N3
 *
 *   Magic byte: LC[2] = 0xAE  ('AE' = AES). Currently always 0x00 in plain
 *   DMR; non-patched receivers parse this as malformed and reject the call.
 *
 *   Nonce: 4 bytes random — N0 in LC[1], N1..N3 in LC[9..11].
 *   Combined with the AES-CTR superframe counter for the IV, giving 32 bits
 *   of nonce randomness per PTT. Collision probability with same key:
 *   ~1 in 4 billion per PTT.
 *
 *   Caller responsibilities:
 *     TX side: aes_patch_lc_steal_apply_tx(spi_tx)         after LC build
 *     RX side: aes_patch_lc_steal_check_and_apply_rx(LCBuf) on each LC frame
 * ========================================================================= */
#define LC_STEAL_MAGIC      0xAE
#define LC_STEAL_MAGIC_IDX  2
#define LC_STEAL_N0_IDX     1
#define LC_STEAL_N1_IDX     9
#define LC_STEAL_N2_IDX     10
#define LC_STEAL_N3_IDX     11

static uint8_t s_currentTxNonce[8];   /* full 8-byte nonce, last 4 bytes zero */
static uint8_t s_currentRxNonceFromLC[8];
static uint32_t s_lastSeenLcNonce32 = 0;
static uint8_t s_lastSeenLcValid = 0;

/* AES patch: per-call autodetect gate for PTT mode.
 *   1 = this incoming call has the magic byte, decrypt voice
 *   0 = this incoming call is plaintext, play through clear
 * Set/cleared by aes_patch_lc_steal_check_and_apply_rx() on each LC parse.
 * Read by the RX voice tap to decide whether to decrypt this frame.
 *
 * In Det mode this flag is always 1 (no autodetect — Det mode can't
 * distinguish encrypted from plaintext, so it always decrypts).
 *
 * Initialized to 1 so Det mode and "we haven't seen an LC yet" both
 * default to "try to decrypt" (the original behavior). */
static uint8_t s_rxAutodetectAllowDecrypt = 1;

int dmr_crypto_rx_should_decrypt_this_call(void)
{
    return s_rxAutodetectAllowDecrypt ? 1 : 0;
}

/* Generate a fresh per-PTT nonce (4 random bytes + 4 zero bytes), init the
 * TX context with it, and return the 4-byte short-nonce ready for LC stuffing.
 * Returns 0 if Option A is not engaged. */
uint32_t aes_patch_lc_steal_make_tx_nonce(void)
{
    /* AES patch: bail if engage hasn't run, slot index out of range, or
     * slot is empty (flags has KEY_FLAG_SET cleared). The check covers
     * both the "engage said empty" path (s_lastFlags == 0) and the
     * pre-boot initial 0xFF state (s_lastIdx > KEY_SLOT_COUNT). */
    if (!s_lastChan || s_lastIdx == 0 || s_lastIdx > KEY_SLOT_COUNT
        || (s_lastFlags & KEY_FLAG_SET) == 0) {
        return 0;
    }
    const KeySlot_t *ks = keystore_get(s_lastIdx);
    if (!ks || (ks->flags & KEY_FLAG_SET) == 0) {
        return 0;
    }
    if (ks->nonceMode != NONCE_MODE_A_LC_STEAL) {
        return 0;
    }
    /* Get 4 fresh random bytes from the hardware RNG. */
    uint8_t shortNonce[4];
    uint32_t r = rng_word();
    shortNonce[0] = (uint8_t)(r >> 24);
    shortNonce[1] = (uint8_t)(r >> 16);
    shortNonce[2] = (uint8_t)(r >> 8);
    shortNonce[3] = (uint8_t)(r);
    memset(s_currentTxNonce, 0, 8);
    memcpy(s_currentTxNonce, shortNonce, 4);
    dmr_crypto_tx_init(ks->key, s_currentTxNonce);
    return r;
}

/* TX-side: stuff the 4-byte nonce into the LC bytes at the magic positions.
 * Call this AFTER the LC is built but BEFORE SPI0WritePageRegByteArray
 * commits it to the chip. No-op if the active slot isn't in mode A. */
void aes_patch_lc_steal_apply_tx(uint8_t *spi_tx)
{
    uint32_t r = aes_patch_lc_steal_make_tx_nonce();
    if (r == 0) {
        return;  /* Not in mode A — leave LC bytes alone. */
    }
    spi_tx[LC_STEAL_MAGIC_IDX] = LC_STEAL_MAGIC;
    spi_tx[LC_STEAL_N0_IDX]    = (uint8_t)(r >> 24);
    spi_tx[LC_STEAL_N1_IDX]    = (uint8_t)(r >> 16);
    spi_tx[LC_STEAL_N2_IDX]    = (uint8_t)(r >> 8);
    spi_tx[LC_STEAL_N3_IDX]    = (uint8_t)(r);
}

/* RX-side: examine an incoming LC frame. If it has the magic byte and
 * the active slot is in mode PTT (A_LC_STEAL), extract the nonce and
 * re-init RX with it. Idempotent.
 *
 * Also sets the s_rxAutodetectAllowDecrypt flag for the voice tap:
 *   - magic byte present, slot in PTT mode -> allow decrypt (encrypted call)
 *   - no magic byte, slot in PTT mode      -> SKIP decrypt (plaintext call)
 *   - any LC, slot in Det mode             -> always allow decrypt
 *   - encryption not engaged at all        -> flag value doesn't matter,
 *                                              taps already skip via
 *                                              dmr_crypto_rx_active()==0 */
void aes_patch_lc_steal_check_and_apply_rx(const uint8_t *LCBuf)
{
    /* Default to allowing decrypt; Det-mode and fresh state both want this. */
    s_rxAutodetectAllowDecrypt = 1;

    /* AES patch: bail if no slot engaged or slot is empty. */
    if (!s_lastChan || s_lastIdx == 0 || s_lastIdx > KEY_SLOT_COUNT
        || (s_lastFlags & KEY_FLAG_SET) == 0) {
        s_lastSeenLcValid = 0;
        return;
    }
    const KeySlot_t *ks = keystore_get(s_lastIdx);
    if (!ks || (ks->flags & KEY_FLAG_SET) == 0) {
        s_lastSeenLcValid = 0;
        return;
    }
    if (ks->nonceMode != NONCE_MODE_A_LC_STEAL) {
        /* Det mode: no autodetect; always allow decrypt. */
        return;
    }

    /* PTT (LC-steal) mode: gate decrypt on magic byte presence. */
    if (LCBuf[LC_STEAL_MAGIC_IDX] != LC_STEAL_MAGIC) {
        /* Plaintext call on encrypted-channel-with-PTT-mode-slot.
         * Skip decrypt so the audio plays through clear. */
        s_rxAutodetectAllowDecrypt = 0;
        s_lastSeenLcValid = 0;
        return;
    }

    /* Magic present — encrypted call. Allow decrypt and extract nonce. */
    uint32_t nonce32 = ((uint32_t)LCBuf[LC_STEAL_N0_IDX] << 24)
                     | ((uint32_t)LCBuf[LC_STEAL_N1_IDX] << 16)
                     | ((uint32_t)LCBuf[LC_STEAL_N2_IDX] << 8)
                     |  (uint32_t)LCBuf[LC_STEAL_N3_IDX];
    if (s_lastSeenLcValid && nonce32 == s_lastSeenLcNonce32) {
        return;  /* same call, no re-init */
    }
    memset(s_currentRxNonceFromLC, 0, 8);
    s_currentRxNonceFromLC[0] = (uint8_t)(nonce32 >> 24);
    s_currentRxNonceFromLC[1] = (uint8_t)(nonce32 >> 16);
    s_currentRxNonceFromLC[2] = (uint8_t)(nonce32 >> 8);
    s_currentRxNonceFromLC[3] = (uint8_t)(nonce32);
    dmr_crypto_rx_init(ks->key, s_currentRxNonceFromLC);
    s_lastSeenLcNonce32 = nonce32;
    s_lastSeenLcValid = 1;
}

