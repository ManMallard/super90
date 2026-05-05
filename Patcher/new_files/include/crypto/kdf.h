#ifndef _OPENGD77_CRYPTO_KDF_H_
#define _OPENGD77_CRYPTO_KDF_H_

#include <stdint.h>
#include <stddef.h>

#define PBKDF2_ITERATIONS 4000U

void pbkdf2_hmac_sha256(const uint8_t *password, size_t passLen,
                        const uint8_t *salt,     size_t saltLen,
                        uint32_t iterations,
                        uint8_t *out, size_t outLen);

#endif
