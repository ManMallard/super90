/*
 * Encryption-key bank — 16 slots persisted to EEPROM.
 *
 * Layout in EEPROM at KEYSTORE_EEPROM_ADDR:
 *   [0..3]   magic word 'O' 'K' 'Y' '1'
 *   [4..N]   array of KEY_SLOT_COUNT KeySlot_t
 *
 * Each KeySlot_t holds a label (12 bytes), an entry-mode byte, a flags
 * byte, and the 32-byte derived AES-256 key.
 */
#include <string.h>
#include "crypto/key_storage.h"
#include "hardware/EEPROM.h"

#define KS_MAGIC 0x4F4B5935u   /* 'OKY5' — bumped from OKY4 when default new-slot mode changed to PTT */

typedef struct {
    uint32_t magic;
    KeySlot_t slot[KEY_SLOT_COUNT];
} KeyBank_t;

static KeyBank_t s_bank;
static uint8_t   s_loaded = 0;

static void ensure_loaded(void)
{
    if (s_loaded) return;
    EEPROM_Read(KEYSTORE_EEPROM_ADDR, (uint8_t *)&s_bank, sizeof(s_bank));
    if (s_bank.magic != KS_MAGIC) {
        memset(&s_bank, 0, sizeof(s_bank));
        s_bank.magic = KS_MAGIC;
        /* AES patch: default new bank to PTT mode (per-PTT nonce). */
        for (int i = 0; i < KEY_SLOT_COUNT; i++) {
            s_bank.slot[i].nonceMode = NONCE_MODE_A_LC_STEAL;
        }
    }
    /* AES patch: normalize stale nonceMode values. Option B was removed,
     * so any slot with nonceMode > A_LC_STEAL falls back to DETERMINISTIC. */
    for (int i = 0; i < KEY_SLOT_COUNT; i++) {
        if (s_bank.slot[i].nonceMode > NONCE_MODE_A_LC_STEAL) {
            s_bank.slot[i].nonceMode = NONCE_MODE_DETERMINISTIC;
        }
    }
    s_loaded = 1;
}

void keystore_init(void) { ensure_loaded(); }

void keystore_save(void)
{
    s_bank.magic = KS_MAGIC;
    EEPROM_Write(KEYSTORE_EEPROM_ADDR, (uint8_t *)&s_bank, sizeof(s_bank));
}

const KeySlot_t *keystore_get(uint8_t slot1based)
{
    ensure_loaded();
    if (slot1based == 0 || slot1based > KEY_SLOT_COUNT) return 0;
    return &s_bank.slot[slot1based - 1];
}

KeySlot_t *keystore_get_mut(uint8_t slot1based)
{
    ensure_loaded();
    if (slot1based == 0 || slot1based > KEY_SLOT_COUNT) return 0;
    return &s_bank.slot[slot1based - 1];
}

void keystore_clear(uint8_t slot1based)
{
    ensure_loaded();
    if (slot1based == 0 || slot1based > KEY_SLOT_COUNT) return;
    memset(&s_bank.slot[slot1based - 1], 0, sizeof(KeySlot_t));
    /* AES patch: cleared slots default to PTT mode (per-PTT nonce). */
    s_bank.slot[slot1based - 1].nonceMode = NONCE_MODE_A_LC_STEAL;
    keystore_save();
}

int keystore_is_set(uint8_t slot1based)
{
    const KeySlot_t *s = keystore_get(slot1based);
    if (!s) return 0;
    return (s->flags & KEY_FLAG_SET) ? 1 : 0;
}
