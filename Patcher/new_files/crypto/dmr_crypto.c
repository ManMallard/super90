#include "dmr_crypto.h"
#include "aes.h"
#include <string.h>

/* Implementations of these come from the OpenGD77 codebase: */
extern uint32_t ticksGetMillis(void);   /* monotonic ms — for nonce mixing */
extern uint32_t getRandom(void);        /* if available; otherwise see below */

static struct AES_ctx s_ctx_tx;
static struct AES_ctx s_ctx_rx;
static uint8_t        s_nonce_tx[DMR_NONCE_BYTES];
static uint8_t        s_nonce_rx[DMR_NONCE_BYTES];
static bool           s_tx_active;
static bool           s_rx_active;

static void build_iv(uint8_t iv[16], const uint8_t nonce[DMR_NONCE_BYTES], uint32_t sfn)
{
    memcpy(iv, nonce, DMR_NONCE_BYTES);
    iv[8]  = (uint8_t)(sfn >> 24);
    iv[9]  = (uint8_t)(sfn >> 16);
    iv[10] = (uint8_t)(sfn >>  8);
    iv[11] = (uint8_t)(sfn      );
    iv[12] = iv[13] = iv[14] = iv[15] = 0x00;
}

void dmr_crypto_tx_init(const uint8_t key[32], const uint8_t nonce[DMR_NONCE_BYTES])
{
    AES_init_ctx(&s_ctx_tx, key);
    memcpy(s_nonce_tx, nonce, DMR_NONCE_BYTES);
    s_tx_active = true;
}

void dmr_crypto_rx_init(const uint8_t key[32], const uint8_t nonce[DMR_NONCE_BYTES])
{
    AES_init_ctx(&s_ctx_rx, key);
    memcpy(s_nonce_rx, nonce, DMR_NONCE_BYTES);
    s_rx_active = true;
}

void dmr_crypto_tx_clear(void) { memset(&s_ctx_tx, 0, sizeof s_ctx_tx); memset(s_nonce_tx, 0, DMR_NONCE_BYTES); s_tx_active = false; }
void dmr_crypto_rx_clear(void) { memset(&s_ctx_rx, 0, sizeof s_ctx_rx); memset(s_nonce_rx, 0, DMR_NONCE_BYTES); s_rx_active = false; }

bool dmr_crypto_tx_active(void) { return s_tx_active; }
bool dmr_crypto_rx_active(void) { return s_rx_active; }

void dmr_crypto_tx_frame(uint8_t buf[DMR_AMBE_PAYLOAD_BYTES], uint32_t sfn)
{
    if (!s_tx_active) return;
    uint8_t iv[16];
    build_iv(iv, s_nonce_tx, sfn);
    AES_ctx_set_iv(&s_ctx_tx, iv);
    AES_CTR_xcrypt_buffer(&s_ctx_tx, buf, DMR_AMBE_PAYLOAD_BYTES);
}

void dmr_crypto_rx_frame(uint8_t buf[DMR_AMBE_PAYLOAD_BYTES], uint32_t sfn)
{
    if (!s_rx_active) return;
    uint8_t iv[16];
    build_iv(iv, s_nonce_rx, sfn);
    AES_ctx_set_iv(&s_ctx_rx, iv);
    AES_CTR_xcrypt_buffer(&s_ctx_rx, buf, DMR_AMBE_PAYLOAD_BYTES);
}

/*
 * Nonce generator. The MK22 has SIM_UID and a TRNG (RNGA) on some variants.
 * This fallback mixes the millisecond tick with the chip UID for bench use.
 * Replace with a hardware-RNG read if available on your build.
 */
void dmr_crypto_make_nonce(uint8_t nonce[DMR_NONCE_BYTES])
{
    extern volatile uint32_t SIM_UIDH, SIM_UIDMH, SIM_UIDML, SIM_UIDL;
    uint32_t a = ticksGetMillis() ^ SIM_UIDL;
    uint32_t b = (uint32_t)(uintptr_t)nonce ^ SIM_UIDMH;
    nonce[0] = (uint8_t)(a      ); nonce[1] = (uint8_t)(a >>  8);
    nonce[2] = (uint8_t)(a >> 16); nonce[3] = (uint8_t)(a >> 24);
    nonce[4] = (uint8_t)(b      ); nonce[5] = (uint8_t)(b >>  8);
    nonce[6] = (uint8_t)(b >> 16); nonce[7] = (uint8_t)(b >> 24);
}
