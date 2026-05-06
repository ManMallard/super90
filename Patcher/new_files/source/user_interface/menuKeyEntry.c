/*
 * Key entry menu — three modes dispatched from a single MENU_KEY_ENTRY id:
 *   MODE_PASS  — T9 multi-tap passphrase (then PBKDF2 to derive 32 bytes)
 *   MODE_HEX   — 64-char hex entry (direct 32 bytes)
 *   MODE_LABEL — text label up to 12 chars
 *
 * The mode + slot are set via menuKeyEntry_*(slot1based) before the menu
 * is pushed; menuKeyEntry() routes each event accordingly.
 *
 * AES patch UI changes:
 *   - Passphrase characters are NOT masked during entry (full cleartext).
 *   - When re-entering an already-set slot, the existing passphrase is
 *     loaded as a starting point so the user can edit it.
 *   - KEY_LEFT performs backspace (one char delete).
 *   - The help line shows BACK/SAVE/CANCEL hints.
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
static char     s_buf[KEY_PASSPHRASE_MAX + 1];   /* passphrase or label buffer */
static char     s_hexbuf[64 + 1];                /* hex buffer (separate, larger) */
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

static char *active_buf(void)
{
    return (s_mode == MODE_HEX) ? s_hexbuf : s_buf;
}

static int active_buf_size(void)
{
    return (s_mode == MODE_HEX) ? (int)sizeof(s_hexbuf) : (int)sizeof(s_buf);
}

static void render(void)
{
    displayClearBuf();

    /* AES patch: compact layout for ~64px-tall display (HX8353E).
     *
     *   Row  0 (12px): "Slot N: <Title>"  e.g. "Slot 3: Passphrase"
     *   Row 14 (10px): "Cur: ***X***X..."  (only if re-entering populated)
     *   Row 26 (16px): live edit buffer  -- the typing area
     *   Row 50 (10px): "L<- G=ok R=x"   -- compact help line
     */
    char titleLine[28];
    const char *title = (s_mode == MODE_PASS)  ? "Passphrase"
                      : (s_mode == MODE_HEX)   ? "Hex Key"
                      : (s_mode == MODE_LABEL) ? "Label"
                      : "Entry";
    snprintf(titleLine, sizeof(titleLine), "Slot %u: %s", (unsigned)s_slot, title);
    displayPrintAt(0, 0, titleLine, FONT_SIZE_1);

    /* AES patch: if re-entering a populated PASSPHRASE slot, show the
     * stored asterisk pattern as a read-only reference line. */
    if (s_mode == MODE_PASS) {
        const KeySlot_t *ks = keystore_get(s_slot);
        if (ks && (ks->flags & KEY_FLAG_SET)
            && ks->entryMode == KEY_ENTRY_PASSPHRASE
            && ks->passPreview[0] != 0)
        {
            char cur[KEY_PASS_PREVIEW_LEN + 8];
            char preview[KEY_PASS_PREVIEW_LEN + 1] = {0};
            memcpy(preview, ks->passPreview, KEY_PASS_PREVIEW_LEN);
            snprintf(cur, sizeof(cur), "Cur:%s", preview);
            displayPrintAt(0, 14, cur, FONT_SIZE_1);
        }
    }

    /* AES patch: passphrase shown in cleartext (no masking) so the user
     * can verify what they typed. Only the slot LIST shows the masked
     * preview pattern. */
    char shown[33] = {0};
    const char *src = active_buf();
    int show = s_len > 30 ? 30 : s_len;
    for (int i = 0; i < show; ++i) shown[i] = src[i];
    shown[show] = 0;
    displayPrintAt(0, 26, shown, FONT_SIZE_2);

    displayPrintAt(0, 50, "L<- G=ok R=x", FONT_SIZE_1);
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
    s_hexbuf[s_len++] = (hexnyb < 10) ? ('0' + hexnyb) : ('a' + hexnyb - 10);
    s_hexbuf[s_len] = 0;
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

    /* AES patch: store ONLY the asterisk pattern (every 4th char visible),
     * not the cleartext. This prevents the passphrase from being recovered
     * if the EEPROM is dumped, while still letting the user verify they
     * have the right slot. */
    memset(ks->passPreview, 0, sizeof(ks->passPreview));
    int n = (s_len < KEY_PASS_PREVIEW_LEN) ? s_len : KEY_PASS_PREVIEW_LEN;
    for (int i = 0; i < n; i++) {
        ks->passPreview[i] = (((i + 1) % 4) == 0) ? s_buf[i] : '*';
    }

    if (ks->label[0] == 0) {
        snprintf(ks->label, KEY_LABEL_LEN, "Slot %u", (unsigned)s_slot);
    }
    keystore_save();

    /* AES patch: scrub the cleartext from RAM as soon as it's been hashed. */
    memset(s_buf, 0, sizeof(s_buf));
    s_len = 0;
}

static void commit_hex(void)
{
    if (s_len != 64) return;
    KeySlot_t *ks = keystore_get_mut(s_slot);
    if (!ks) return;
    for (int i = 0; i < 32; ++i) {
        int hi = unhex(s_hexbuf[i*2]);
        int lo = unhex(s_hexbuf[i*2+1]);
        if (hi < 0 || lo < 0) return;
        ks->key[i] = (uint8_t)((hi << 4) | lo);
    }
    ks->entryMode = KEY_ENTRY_HEX;
    ks->flags |= KEY_FLAG_SET;
    /* Hex entries don't have a passphrase to preview. */
    memset(ks->passPreview, 0, sizeof(ks->passPreview));
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

static void prepopulate_from_slot(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    memset(s_hexbuf, 0, sizeof(s_hexbuf));
    s_len = 0;
    s_t9_lastKey = 0xFF;
    s_t9_tapIndex = 0;
    s_t9_lastMs = 0;

    /* AES patch: passphrase mode CANNOT prepopulate — only the asterisk
     * pattern is stored, not the cleartext, so recovery is impossible.
     * Re-entering a passphrase always starts from an empty buffer. */
    const KeySlot_t *ks = keystore_get(s_slot);
    if (!ks || !(ks->flags & KEY_FLAG_SET)) return;

    if (s_mode == MODE_LABEL && ks->label[0] != 0)
    {
        char lab[KEY_LABEL_LEN + 1] = {0};
        memcpy(lab, ks->label, KEY_LABEL_LEN);
        size_t ln = strnlen(lab, KEY_LABEL_LEN);
        memcpy(s_buf, lab, ln);
        s_buf[ln] = 0;
        s_len = (int)ln;
    }
    /* Hex mode and passphrase mode start empty. */
}

/* Routed event handler. */
static menuStatus_t common_event(uiEvent_t *ev, bool isFirstRun)
{
    if (isFirstRun) {
        prepopulate_from_slot();
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
        if (s_len > 0) {
            char *buf = active_buf();
            buf[--s_len] = 0;
        }
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
