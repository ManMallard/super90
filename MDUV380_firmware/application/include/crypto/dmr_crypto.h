#ifndef _OPENGD77_CRYPTO_DMR_CRYPTO_H_
#define _OPENGD77_CRYPTO_DMR_CRYPTO_H_

#include <stdint.h>

#define DMR_AMBE_FRAME_BYTES 27
#define DMR_NONCE_BYTES       8

void dmr_crypto_make_nonce(uint8_t nonce[DMR_NONCE_BYTES]);

void dmr_crypto_tx_init (const uint8_t key[32], const uint8_t nonce[8]);
void dmr_crypto_tx_clear(void);
int  dmr_crypto_tx_active(void);
void dmr_crypto_tx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES]);

void dmr_crypto_rx_init (const uint8_t key[32], const uint8_t nonce[8]);
void dmr_crypto_rx_clear(void);
int  dmr_crypto_rx_active(void);
void dmr_crypto_rx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES]);

/* High-level: engage or disengage encryption based on currentChannelData.
 * Called on PTT-down (TX path) and on channel-change (RX path). */
void aes_patch_engage_for_current_channel(void);

/* AES patch Option A — LC-steal nonce wire format.
 * TX side: call from HR-C6000.c right before SPI0WritePageRegByteArray
 * commits the LC. Generates per-PTT random nonce and stuffs it into
 * stolen LC bytes. No-op when active slot isn't in mode A.
 * RX side: call from hrc6000HandleLCData on each LC parse — checks for
 * the magic byte and re-inits RX with the extracted nonce. */
void aes_patch_lc_steal_apply_tx(uint8_t *spi_tx);
void aes_patch_lc_steal_check_and_apply_rx(const uint8_t *LCBuf);

/* Per-call gate for RX decryption (autodetect for PTT mode):
 *   1 = decrypt the next voice frame
 *   0 = skip decryption (plaintext call detected on a PTT-mode channel)
 * Always 1 in Det mode and when no LC has been parsed yet. */
int dmr_crypto_rx_should_decrypt_this_call(void);

#endif
