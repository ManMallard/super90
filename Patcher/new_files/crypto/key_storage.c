#include "key_storage.h"
#include "kdf.h"
#include <string.h>
#include <ctype.h>

/*
 * NV layer: OpenGD77 stores nonVolatileSettings in flash (settings.c).
 * For minimal disruption we keep the key bank in a dedicated EEPROM region
 * via these two helpers, which the patch wires up to the existing EEPROM
 * driver. If your tree doesn't expose them, replace with direct calls to
 * EEPROM_Write / EEPROM_Read on a free address range (see patches/0006).
 */
extern bool EEPROM_Write(uint32_t addr, const uint8_t *buf, uint32_t len);
extern bool EEPROM_Read (uint32_t addr,       uint8_t *buf, uint32_t len);

/* Pick a free EEPROM range. Verify against your codeplug map before flashing. */
#ifndef KEYSTORE_EEPROM_ADDR
#define KEYSTORE_EEPROM_ADDR  0x0001F000UL
#endif

#define KEYSTORE_MAGIC 0x4B53544FUL  /* "OTSK" */

static struct {
    uint32_t   magic;
    key_slot_t slots[KEY_SLOT_COUNT];
} __attribute__((packed)) s_bank;

static uint8_t hexnyb(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0xFF;
}

void keystore_init(void)
{
    memset(&s_bank, 0, sizeof s_bank);
    s_bank.magic = KEYSTORE_MAGIC;
}

void keystore_load(void)
{
    if (!EEPROM_Read(KEYSTORE_EEPROM_ADDR, (uint8_t *)&s_bank, sizeof s_bank)
        || s_bank.magic != KEYSTORE_MAGIC) {
        keystore_init();
    }
}

void keystore_save(void)
{
    s_bank.magic = KEYSTORE_MAGIC;
    EEPROM_Write(KEYSTORE_EEPROM_ADDR, (const uint8_t *)&s_bank, sizeof s_bank);
}

bool keystore_slot_in_use(uint8_t slot1)
{
    if (slot1 == 0 || slot1 > KEY_SLOT_COUNT) return false;
    return s_bank.slots[slot1 - 1].mode != KEY_ENTRY_EMPTY;
}

void keystore_clear_slot(uint8_t slot1)
{
    if (slot1 == 0 || slot1 > KEY_SLOT_COUNT) return;
    memset(&s_bank.slots[slot1 - 1], 0, sizeof(key_slot_t));
}

const key_slot_t *keystore_get(uint8_t slot1)
{
    if (slot1 == 0 || slot1 > KEY_SLOT_COUNT) return NULL;
    if (s_bank.slots[slot1 - 1].mode == KEY_ENTRY_EMPTY) return NULL;
    return &s_bank.slots[slot1 - 1];
}

void keystore_set_passphrase(uint8_t slot1, const char *pass, const char *label)
{
    if (slot1 == 0 || slot1 > KEY_SLOT_COUNT || pass == NULL) return;
    key_slot_t *s = &s_bank.slots[slot1 - 1];

    /* Salt: fixed device tag + slot index — same passphrase yields different
     * keys per slot, predictable on this device for cross-radio interop with
     * a deliberate pairing exchange. */
    uint8_t salt[6] = { 'O', 'G', 'D', '7', '7', slot1 };
    pbkdf2_sha256(pass, strlen(pass), salt, sizeof salt, 4000, s->key);

    memset(s->label, 0, sizeof s->label);
    if (label) {
        size_t n = strlen(label);
        if (n > KEY_SLOT_LABEL_LEN) n = KEY_SLOT_LABEL_LEN;
        memcpy(s->label, label, n);
    }
    s->mode = KEY_ENTRY_PASSPHRASE;
}

bool keystore_set_hex(uint8_t slot1, const char *hex64, const char *label)
{
    if (slot1 == 0 || slot1 > KEY_SLOT_COUNT || hex64 == NULL) return false;
    if (strlen(hex64) != 64) return false;

    uint8_t tmp[KEY_SLOT_BYTES];
    for (int i = 0; i < KEY_SLOT_BYTES; ++i) {
        uint8_t hi = hexnyb(hex64[2*i]);
        uint8_t lo = hexnyb(hex64[2*i + 1]);
        if (hi == 0xFF || lo == 0xFF) return false;
        tmp[i] = (uint8_t)((hi << 4) | lo);
    }

    key_slot_t *s = &s_bank.slots[slot1 - 1];
    memcpy(s->key, tmp, KEY_SLOT_BYTES);
    memset(s->label, 0, sizeof s->label);
    if (label) {
        size_t n = strlen(label);
        if (n > KEY_SLOT_LABEL_LEN) n = KEY_SLOT_LABEL_LEN;
        memcpy(s->label, label, n);
    }
    s->mode = KEY_ENTRY_HEX;
    memset(tmp, 0, sizeof tmp);
    return true;
}
