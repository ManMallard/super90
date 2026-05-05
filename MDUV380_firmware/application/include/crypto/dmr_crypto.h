#ifndef _OPENGD77_CRYPTO_DMR_CRYPTO_H_
#define _OPENGD77_CRYPTO_DMR_CRYPTO_H_

#include <stdint.h>

#define DMR_AMBE_FRAME_BYTES 27
#define DMR_NONCE_BYTES       8

void dmr_crypto_make_nonce(uint8_t nonce[DMR_NONCE_BYTES]);

void dmr_crypto_tx_init (const uint8_t key[32], const uint8_t nonce[8]);
void dmr_crypto_tx_clear(void);
int  dmr_crypto_tx_active(void);
void dmr_crypto_tx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES], uint32_t superframe);

void dmr_crypto_rx_init (const uint8_t key[32], const uint8_t nonce[8]);
void dmr_crypto_rx_clear(void);
int  dmr_crypto_rx_active(void);
void dmr_crypto_rx_frame(uint8_t buf[DMR_AMBE_FRAME_BYTES], uint32_t superframe);

#endif
