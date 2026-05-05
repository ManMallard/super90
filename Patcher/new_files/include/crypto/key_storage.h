#ifndef _OPENGD77_CRYPTO_KEY_STORAGE_H_
#define _OPENGD77_CRYPTO_KEY_STORAGE_H_

#include <stdint.h>

#define KEY_SLOT_COUNT          16
#define KEY_LABEL_LEN           12
#define KEY_BYTES               32   /* AES-256 */

#define KEY_ENTRY_PASSPHRASE     1
#define KEY_ENTRY_HEX            2

#define KEY_FLAG_SET             0x01

#define KEYSTORE_EEPROM_ADDR     0x0001F000   /* edit if your codeplug uses this region */

typedef struct {
    char     label[KEY_LABEL_LEN];
    uint8_t  entryMode;     /* KEY_ENTRY_PASSPHRASE | KEY_ENTRY_HEX */
    uint8_t  flags;
    uint8_t  reserved[2];
    uint8_t  key[KEY_BYTES];
} KeySlot_t;

void keystore_init(void);
void keystore_save(void);

const KeySlot_t *keystore_get(uint8_t slot1based);
KeySlot_t       *keystore_get_mut(uint8_t slot1based);
void             keystore_clear(uint8_t slot1based);
int              keystore_is_set(uint8_t slot1based);

#endif
