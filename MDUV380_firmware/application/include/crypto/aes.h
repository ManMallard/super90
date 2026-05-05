/*
 * AES-256 CTR public interface.
 */
#ifndef _OPENGD77_CRYPTO_AES_H_
#define _OPENGD77_CRYPTO_AES_H_

#include <stdint.h>
#include <stddef.h>

#define AES_BLOCKLEN  16
#define AES_KEYLEN    32   /* AES-256 */
#define AES_keyExpSize 240 /* (Nb*(Nr+1)*4) for AES-256 */

struct AES_ctx {
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx_iv(struct AES_ctx *ctx, const uint8_t *key, const uint8_t *iv);
void AES_CTR_xcrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, size_t length);

#endif
