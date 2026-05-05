/*
 * Key entry menu — three modes dispatched from a single MENU_KEY_ENTRY id:
 *   MODE_PASS  — T9 multi-tap passphrase (then PBKDF2 to derive 32 bytes)
 *   MODE_HEX   — 64-char hex entry (direct 32 bytes)
 *   MODE_LABEL — text label up to 12 chars
 *
 * The mode + slot are set via menuKeyEntry_*(slot1based) before the menu
 * is pushed; menuKeyEntry() routes each event accordingly.
 */
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "io/keyboard.h"
#include "hardware/HX8353E.h"
#include "functions/ticks.h"        /* ticksGetMillis() — native */

#include "crypto/key_storage.h"
#include "crypto/kdf.h"

#define MODE_PASS  1
#define MODE_HEX   2
#define MODE_LABEL 3

static uint8_t  s_slot;
static uint8_t  s_mode;
static char     s_buf[64 + 1];
static int      s_len;
static uint8_t  s_t9_lastKey;
static uint8_t  s_t9_tapIndex;
static uint32_t s_t9_lastMs;

/* T9 mapping — standard mobile keypad. */
static const char *t9map[10] = {
    " 0",       /* 0 */
    "1.,?!",    /* 1 */
    "abc2ABC",  /* 2 */
    "def3DEF",  /* 3 */
    "ghi4GHI",  /* 4 */
    "jkl5JKL",  /* 5 */
    "mno6MNO",  /* 6 */
    "pqrs7PQRS",/* 7 */
    "tuv8TUV",  /* 8 */
    "wxyz9WXYZ" /* 9 */
};

static void render(void)
{
    displayClearBuf();
    const char *title = (s_mode == MODE_PASS)  ? "Passphrase"
                      : (s_mode == MODE_HEX)   ? "Hex Key"
                      : (s_mode == MODE_LABEL) ? "Label"
                      : "Entry";
    displayPrintCentered(0, title, FONT_SIZE_2);

    char header[20];
    snprintf(header, sizeof(header), "Slot %u", (unsigned)s_slot);
    displayPrintAt(2, 16, header, FONT_SIZE_1);

    /* Show what's been typed (passphrase masked unless it's the last char
     * within the T9 commit window). */
    char shown[20] = {0};
    int show = s_len > 18 ? 18 : s_len;
    for (int i = 0; i < show; ++i) {
        bool last = (i == s_len - 1)
                  && ((ticksGetMillis() - s_t9_lastMs) < 700);
        shown[i] = (s_mode == MODE_PASS && !last) ? '*' : s_buf[i];
    }
    shown[show] = 0;
    displayPrintAt(2, 28, shown, FONT_SIZE_2);
    displayPrintAt(2, 48, "GREEN=save  RED=cancel", FONT_SIZE_1);
    displayRender();
}

static void t9_tap(int digit)
{
    uint32_t now = ticksGetMillis();
    bool sameKey = (digit == s_t9_lastKey) && ((now - s_t9_lastMs) < 700);
    if (sameKey) {
        const char *opts = t9map[digit];
        s_t9_tapIndex = (s_t9_tapIndex + 1) % (uint8_t)strlen(opts);
        if (s_len > 0) s_buf[s_len - 1] = opts[s_t9_tapIndex];
    } else {
        if (s_len < (int)sizeof(s_buf) - 1) {
            const char *opts = t9map[digit];
            s_buf[s_len++] = opts[0];
            s_buf[s_len] = 0;
            s_t9_tapIndex = 0;
        }
    }
    s_t9_lastKey = (uint8_t)digit;
    s_t9_lastMs  = now;
}

static void hex_tap(int hexnyb)
{
    if (s_len >= 64) return;
    s_buf[s_len++] = (hexnyb < 10) ? ('0' + hexnyb) : ('a' + hexnyb - 10);
    s_buf[s_len] = 0;
}

static int unhex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void commit_passphrase(void)
{
    KeySlot_t *ks = keystore_get_mut(s_slot);
    if (!ks) return;
    static const uint8_t salt[] = "OpenGD77-AES256-v1";
    pbkdf2_hmac_sha256((const uint8_t *)s_buf, (size_t)s_len,
                       salt, sizeof(salt) - 1,
                       PBKDF2_ITERATIONS, ks->key, KEY_BYTES);
    ks->entryMode = KEY_ENTRY_PASSPHRASE;
    ks->flags |= KEY_FLAG_SET;
    if (ks->label[0] == 0) {
        snprintf(ks->label, KEY_LABEL_LEN, "Slot %u", (unsigned)s_slot);
    }
    keystore_save();
}

static void commit_hex(void)
{
    if (s_len != 64) return;
    KeySlot_t *ks = keystore_get_mut(s_slot);
    if (!ks) return;
    for (int i = 0; i < 32; ++i) {
        int hi = unhex(s_buf[i*2]);
        int lo = unhex(s_buf[i*2+1]);
        if (hi < 0 || lo < 0) return;
        ks->key[i] = (uint8_t)((hi << 4) | lo);
    }
    ks->entryMode = KEY_ENTRY_HEX;
    ks->flags |= KEY_FLAG_SET;
    if (ks->label[0] == 0) {
        snprintf(ks->label, KEY_LABEL_LEN, "Slot %u", (unsigned)s_slot);
    }
    keystore_save();
}

static void commit_label(void)
{
    KeySlot_t *ks = keystore_get_mut(s_slot);
    if (!ks) return;
    int n = s_len > KEY_LABEL_LEN ? KEY_LABEL_LEN : s_len;
    memset(ks->label, 0, KEY_LABEL_LEN);
    memcpy(ks->label, s_buf, (size_t)n);
    keystore_save();
}

/* Routed event handler. */
static menuStatus_t common_event(uiEvent_t *ev, bool isFirstRun)
{
    if (isFirstRun) {
        memset(s_buf, 0, sizeof(s_buf));
        s_len = 0;
        s_t9_lastKey = 0xFF;
        s_t9_tapIndex = 0;
        s_t9_lastMs = 0;
        render();
        return MENU_STATUS_SUCCESS;
    }
    if (!ev->hasEvent) return MENU_STATUS_SUCCESS;

    if (KEYCHECK_SHORTUP(ev->keys, KEY_RED)) {
        menuSystemPopPreviousMenu();
        return MENU_STATUS_SUCCESS;
    }
    if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN)) {
        if      (s_mode == MODE_PASS)  commit_passphrase();
        else if (s_mode == MODE_HEX)   commit_hex();
        else if (s_mode == MODE_LABEL) commit_label();
        menuSystemPopPreviousMenu();
        return MENU_STATUS_SUCCESS;
    }
    if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT)) {  /* backspace */
        if (s_len > 0) { s_buf[--s_len] = 0; }
        s_t9_lastKey = 0xFF;
        render();
        return MENU_STATUS_SUCCESS;
    }
    if (s_mode == MODE_PASS || s_mode == MODE_LABEL) {
        if (KEYCHECK_SHORTUP_NUMBER(ev->keys)) {
            t9_tap(ev->keys.key - '0');
            render();
        }
    } else if (s_mode == MODE_HEX) {
        char k = (char)ev->keys.key;
        int n = unhex(k);
        if (n >= 0) {
            hex_tap(n);
            render();
        }
    }
    return MENU_STATUS_SUCCESS;
}

/* Public entry — dispatched via single MENU_KEY_ENTRY id. */
menuStatus_t menuKeyEntry(uiEvent_t *ev, bool isFirstRun)
{
    return common_event(ev, isFirstRun);
}

/* Setters — called from menuKeyManagement before pushing the menu. */
void menuKeyEntry_passphrase(uint8_t slot1based)
{
    s_slot = slot1based;
    s_mode = MODE_PASS;
    menuSystemPushNewMenu(MENU_KEY_ENTRY);
}
void menuKeyEntry_hex(uint8_t slot1based)
{
    s_slot = slot1based;
    s_mode = MODE_HEX;
    menuSystemPushNewMenu(MENU_KEY_ENTRY);
}
void menuKeyEntry_label(uint8_t slot1based)
{
    s_slot = slot1based;
    s_mode = MODE_LABEL;
    menuSystemPushNewMenu(MENU_KEY_ENTRY);
}
