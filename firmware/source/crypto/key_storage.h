/*
 * 16-slot AES key bank stored in non-volatile settings.
 * Slot 0 means "no key"; channels store 1..16 to reference a slot.
 *
 * Each slot holds the 32-byte derived key plus a short label and a flag
 * indicating how the key was entered (T9 passphrase or raw hex).
 *
 * Storage note: keys live alongside other NV settings. For higher assurance
 * on a real deployment, XOR-wrap with the MK22 device UID before persist.
 * Bench-test build keeps it simple.
 */
#ifndef KEY_STORAGE_H
#define KEY_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define KEY_SLOT_COUNT       16
#define KEY_SLOT_BYTES       32
#define KEY_SLOT_LABEL_LEN   12

typedef enum {
    KEY_ENTRY_EMPTY      = 0,
    KEY_ENTRY_PASSPHRASE = 1,
    KEY_ENTRY_HEX        = 2,
} key_entry_mode_t;

typedef struct {
    uint8_t key[KEY_SLOT_BYTES];
    char    label[KEY_SLOT_LABEL_LEN + 1];
    uint8_t mode;        /* key_entry_mode_t */
    uint8_t reserved[3];
} __attribute__((packed)) key_slot_t;

void   keystore_init(void);                                /* call once at boot */
void   keystore_load(void);                                /* read from NV */
void   keystore_save(void);                                /* commit to NV */
bool   keystore_slot_in_use(uint8_t slot1based);
void   keystore_clear_slot(uint8_t slot1based);
const  key_slot_t *keystore_get(uint8_t slot1based);       /* slot 1..16, or NULL */
void   keystore_set_passphrase(uint8_t slot1based, const char *pass, const char *label);
bool   keystore_set_hex(uint8_t slot1based, const char *hex64, const char *label);

#endif
