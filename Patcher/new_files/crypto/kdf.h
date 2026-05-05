/*
 * PBKDF2-HMAC-SHA256 — derives a 256-bit AES key from a passphrase.
 * Keep iteration count moderate (~4000) so entry stays under ~200 ms on the MK22.
 */
#ifndef KDF_H
#define KDF_H

#include <stddef.h>
#include <stdint.h>

#define KDF_KEY_BYTES 32

void pbkdf2_sha256(const char  *pass, size_t passlen,
                   const uint8_t *salt, size_t saltlen,
                   uint32_t iters, uint8_t key_out[KDF_KEY_BYTES]);

#endif
