/*
 * PBKDF2-HMAC-SHA256 (RFC 8018 §5.2).
 *
 * Iteration count is intentionally low (PBKDF2_ITERATIONS = 4000) — radio
 * MCUs are slow and entry latency must be sub-second. This is acceptable
 * because the entropy budget is in the passphrase, not in the KDF.
 */
#include <string.h>
#include <stdint.h>
#include "crypto/kdf.h"
#include "crypto/sha256.h"

#define HASH_LEN  32
#define BLOCK_LEN 64

static void hmac_sha256(const uint8_t *key, size_t keyLen,
                        const uint8_t *msg, size_t msgLen,
                        uint8_t out[HASH_LEN])
{
    uint8_t k_ipad[BLOCK_LEN];
    uint8_t k_opad[BLOCK_LEN];
    uint8_t k_norm[BLOCK_LEN];
    SHA256_CTX ctx;
    uint8_t inner[HASH_LEN];

    memset(k_norm, 0, BLOCK_LEN);
    if (keyLen > BLOCK_LEN) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, keyLen);
        sha256_final(&ctx, k_norm);
    } else {
        memcpy(k_norm, key, keyLen);
    }
    for (int i=0;i<BLOCK_LEN;++i) {
        k_ipad[i] = k_norm[i] ^ 0x36;
        k_opad[i] = k_norm[i] ^ 0x5c;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, BLOCK_LEN);
    sha256_update(&ctx, msg, msgLen);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, BLOCK_LEN);
    sha256_update(&ctx, inner, HASH_LEN);
    sha256_final(&ctx, out);
}

void pbkdf2_hmac_sha256(const uint8_t *password, size_t passLen,
                        const uint8_t *salt,     size_t saltLen,
                        uint32_t iterations,
                        uint8_t *out, size_t outLen)
{
    uint8_t U[HASH_LEN];
    uint8_t T[HASH_LEN];
    uint8_t saltAndIdx[64 + 4];
    size_t produced = 0;
    uint32_t blockIndex = 1;

    while (produced < outLen) {
        memcpy(saltAndIdx, salt, saltLen);
        saltAndIdx[saltLen+0] = (uint8_t)(blockIndex >> 24);
        saltAndIdx[saltLen+1] = (uint8_t)(blockIndex >> 16);
        saltAndIdx[saltLen+2] = (uint8_t)(blockIndex >>  8);
        saltAndIdx[saltLen+3] = (uint8_t)(blockIndex);

        hmac_sha256(password, passLen, saltAndIdx, saltLen+4, U);
        memcpy(T, U, HASH_LEN);

        for (uint32_t i=1;i<iterations;++i) {
            hmac_sha256(password, passLen, U, HASH_LEN, U);
            for (int j=0;j<HASH_LEN;++j) T[j] ^= U[j];
        }

        size_t chunk = (outLen - produced) < HASH_LEN ? (outLen - produced) : HASH_LEN;
        memcpy(out + produced, T, chunk);
        produced += chunk;
        blockIndex++;
    }
}
