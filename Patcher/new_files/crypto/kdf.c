#include "kdf.h"
#include "sha256.h"
#include <string.h>

#define BLOCK 64
#define HASH  32

static void hmac_sha256(const uint8_t *key, size_t keylen,
                        const uint8_t *msg, size_t msglen,
                        uint8_t out[HASH])
{
    uint8_t k[BLOCK];
    uint8_t ipad[BLOCK], opad[BLOCK];
    SHA256_CTX ctx;
    uint8_t inner[HASH];

    memset(k, 0, BLOCK);
    if (keylen > BLOCK) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, keylen);
        sha256_final(&ctx, k);
    } else {
        memcpy(k, key, keylen);
    }
    for (int i = 0; i < BLOCK; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, BLOCK);
    sha256_update(&ctx, msg,  msglen);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, BLOCK);
    sha256_update(&ctx, inner, HASH);
    sha256_final(&ctx, out);
}

void pbkdf2_sha256(const char *pass, size_t passlen,
                   const uint8_t *salt, size_t saltlen,
                   uint32_t iters, uint8_t key_out[KDF_KEY_BYTES])
{
    /* Single block (i=1) is enough — output is 32 bytes, hash output is 32 bytes. */
    uint8_t  saltblock[96];
    uint8_t  U[HASH], T[HASH];

    if (saltlen > sizeof(saltblock) - 4) saltlen = sizeof(saltblock) - 4;
    memcpy(saltblock, salt, saltlen);
    saltblock[saltlen + 0] = 0x00;
    saltblock[saltlen + 1] = 0x00;
    saltblock[saltlen + 2] = 0x00;
    saltblock[saltlen + 3] = 0x01;

    hmac_sha256((const uint8_t *)pass, passlen, saltblock, saltlen + 4, U);
    memcpy(T, U, HASH);
    for (uint32_t n = 1; n < iters; ++n) {
        hmac_sha256((const uint8_t *)pass, passlen, U, HASH, U);
        for (int j = 0; j < HASH; ++j) T[j] ^= U[j];
    }
    memcpy(key_out, T, KDF_KEY_BYTES);

    /* Best-effort wipe. */
    memset(U, 0, sizeof U);
    memset(T, 0, sizeof T);
    memset(saltblock, 0, sizeof saltblock);
}
