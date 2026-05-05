/*
 * DMR voice-frame AES-256-CTR wrapper.
 *
 * Encrypt point: TX side, immediately before the 27-byte AMBE+2 superframe
 *                payload is handed to the HR-C6000 over SPI.
 * Decrypt point: RX side, immediately after the 27-byte payload is read
 *                from the HR-C6000 SPI buffer.
 *
 * IV layout (16 B): nonce[8] || sfn_be32 || 0x00 0x00 0x00 0x00
 *
 *   nonce — 8 random bytes generated at PTT, transmitted in the unencrypted
 *           DMR voice header LC; receiver latches it on call start.
 *   sfn   — 32-bit superframe number (free-running, wraps).
 *
 * The DMR frame number register on the HR-C6000 provides SFN deterministically
 * on both sides, so listeners can resync mid-call.
 */
#ifndef DMR_CRYPTO_H
#define DMR_CRYPTO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DMR_AMBE_PAYLOAD_BYTES 27
#define DMR_NONCE_BYTES         8

void dmr_crypto_tx_init (const uint8_t key[32], const uint8_t nonce[DMR_NONCE_BYTES]);
void dmr_crypto_rx_init (const uint8_t key[32], const uint8_t nonce[DMR_NONCE_BYTES]);
void dmr_crypto_tx_clear(void);
void dmr_crypto_rx_clear(void);
bool dmr_crypto_tx_active(void);
bool dmr_crypto_rx_active(void);

/* Encrypt/decrypt one 27-byte AMBE+2 superframe payload in place. */
void dmr_crypto_tx_frame(uint8_t buf[DMR_AMBE_PAYLOAD_BYTES], uint32_t sfn);
void dmr_crypto_rx_frame(uint8_t buf[DMR_AMBE_PAYLOAD_BYTES], uint32_t sfn);

/* Helper for the firmware to emit a per-call nonce. */
void dmr_crypto_make_nonce(uint8_t nonce[DMR_NONCE_BYTES]);

#endif
