#ifndef _OPENGD77_CRYPTO_KEY_STORAGE_H_
#define _OPENGD77_CRYPTO_KEY_STORAGE_H_

#include <stdint.h>

#define KEY_SLOT_COUNT          16
#define KEY_LABEL_LEN           12
#define KEY_BYTES               32   /* AES-256 */
#define KEY_PASSPHRASE_MAX      32   /* used only as in-RAM entry buffer */
#define KEY_PASS_PREVIEW_LEN    32   /* persistent asterisk-pattern preview */

#define KEY_ENTRY_PASSPHRASE     1
#define KEY_ENTRY_HEX            2

#define KEY_FLAG_SET             0x01

/* AES patch: per-slot nonce mode.
 *   DETERMINISTIC = SHA-256("OpenGD77-NONCE-v1" || key) high 8 bytes.
 *                   Same nonce every PTT. Two radios with same passphrase
 *                   decrypt each other without exchanging anything.
 *                   Cryptographically WEAK (CTR-mode nonce reuse).
 *                   Acceptable for bench testing; default for now.
 *   A_LC_STEAL    = Per-PTT random nonce transmitted in stolen LC bits.
 *                   Zero added latency. Breaks DMR interop with all
 *                   non-patched radios on encrypted PTTs.
 *                   STUB - not yet implemented; falls back to DETERMINISTIC.
 *   B_PREFIX_BURST = Per-PTT random nonce transmitted in a vendor data
 *                    burst before voice. ~120ms added PTT latency.
 *                    Preserves DMR wire-format compatibility.
 *                    STUB - not yet implemented; falls back to DETERMINISTIC.
 */
#define NONCE_MODE_DETERMINISTIC   0
#define NONCE_MODE_A_LC_STEAL      1
#define NONCE_MODE_B_PREFIX_BURST  2

#define KEYSTORE_EEPROM_ADDR     0x0001F000   /* edit if your codeplug uses this region */

typedef struct {
    char     label[KEY_LABEL_LEN];
    uint8_t  entryMode;     /* KEY_ENTRY_PASSPHRASE | KEY_ENTRY_HEX */
    uint8_t  flags;
    uint8_t  nonceMode;     /* AES patch: NONCE_MODE_DETERMINISTIC | A_LC_STEAL | B_PREFIX_BURST */
    uint8_t  reserved[1];
    uint8_t  key[KEY_BYTES];
    /* AES patch: asterisk-preview pattern. Every 4th character of the
     * original passphrase is stored verbatim; all other positions are '*'.
     * Example: "MySecret2024" → "***e***t****" (12 chars).
     * The cleartext passphrase is NEVER stored — only this lossy pattern,
     * which is enough to prove "this is my slot" but not enough to
     * recover the original. NUL-terminated. */
    char     passPreview[KEY_PASS_PREVIEW_LEN + 1];
} KeySlot_t;

void keystore_init(void);
void keystore_save(void);

const KeySlot_t *keystore_get(uint8_t slot1based);
KeySlot_t       *keystore_get_mut(uint8_t slot1based);
void             keystore_clear(uint8_t slot1based);
int              keystore_is_set(uint8_t slot1based);

#endif
