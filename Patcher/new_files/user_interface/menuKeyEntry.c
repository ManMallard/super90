/*
 * menuKeyEntry.c — passphrase (T9), hex, and label entry for one key slot.
 *
 * Modes:
 *   - menuKeyEntry_passphrase(slot): T9 multi-tap, max 32 chars, derives via PBKDF2
 *   - menuKeyEntry_hex(slot):        64 hex chars, parsed directly into the key
 *   - menuKeyEntry_label(slot):      T9 multi-tap, max KEY_SLOT_LABEL_LEN
 *
 * T9 mapping (multi-tap):
 *   2: a b c 2     3: d e f 3     4: g h i 4     5: j k l 5
 *   6: m n o 6     7: p q r s 7   8: t u v 8     9: w x y z 9
 *   0: <space> 0   1: . , ? !
 * STAR toggles upper/lower case. LEFT = backspace. GREEN = confirm.
 */
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "crypto/key_storage.h"

#define MAX_PASS 32
#define MAX_HEX  64
#define T9_TIMEOUT_MS 900

static const char *t9map[10] = {
    "0 ", "1.,?!", "abc2", "def3", "ghi4",
    "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9"
};

typedef enum { MODE_PASS, MODE_HEX, MODE_LABEL } entry_mode_t;

static entry_mode_t s_mode;
static uint8_t      s_slot;
static char         s_buf[MAX_HEX + 1];
static int          s_len;
static uint8_t      s_lastKey;
static int          s_tapIdx;
static uint32_t     s_lastKeyMs;
static bool         s_upperCase;

extern uint32_t ticksGetMillis(void);

static void render(const char *title)
{
    char show[MAX_HEX + 1];
    ucClearBuf();
    ucPrintCentered(0, title, FONT_SIZE_2);

    /* Mask passphrase except last char briefly. */
    if (s_mode == MODE_PASS) {
        for (int i = 0; i < s_len; ++i) {
            if (i == s_len - 1 && (ticksGetMillis() - s_lastKeyMs) < 700)
                show[i] = s_buf[i];
            else
                show[i] = '*';
        }
        show[s_len] = 0;
    } else {
        memcpy(show, s_buf, s_len + 1);
    }
    char header[20];
    snprintf(header, sizeof header, "Slot %u  [%s]", s_slot,
             s_upperCase ? "ABC" : "abc");
    ucPrintAt(0, 14, header, FONT_SIZE_1);
    ucPrintAt(0, 28, show,   FONT_SIZE_2);
    ucPrintCentered(52, "GRN=ok  L=bksp  *=case", FONT_SIZE_1);
    ucRender();
}

static void commit_tap(void)
{
    s_lastKey = 0xFF;
    s_tapIdx  = 0;
}

static void handle_t9(uint8_t k)
{
    const char *row = t9map[k];
    int rowLen = (int)strlen(row);
    uint32_t now = ticksGetMillis();

    if (k == s_lastKey && (now - s_lastKeyMs) < T9_TIMEOUT_MS && s_len > 0) {
        s_tapIdx = (s_tapIdx + 1) % rowLen;
        char c = row[s_tapIdx];
        s_buf[s_len - 1] = (s_upperCase && isalpha((unsigned char)c)) ? (char)toupper((int)c) : c;
    } else {
        int max = (s_mode == MODE_LABEL) ? KEY_SLOT_LABEL_LEN : MAX_PASS;
        if (s_len >= max) return;
        s_tapIdx = 0;
        char c = row[0];
        s_buf[s_len++] = (s_upperCase && isalpha((unsigned char)c)) ? (char)toupper((int)c) : c;
        s_buf[s_len]   = 0;
    }
    s_lastKey   = k;
    s_lastKeyMs = now;
}

static void handle_hex(uint8_t k)
{
    if (s_len >= MAX_HEX) return;
    s_buf[s_len++] = (char)('0' + k);
    s_buf[s_len]   = 0;
}

static menuStatus_t common_event(uiEvent_t *ev, const char *title)
{
    if ((ev->events & KEY_EVENT) == 0) return MENU_STATUS_SUCCESS;

    /* Digit keys */
    for (uint8_t k = 0; k <= 9; ++k) {
        if (KEYCHECK_PRESS(ev->keys, KEY_0 + k)) {
            if (s_mode == MODE_HEX) handle_hex(k);
            else                    handle_t9(k);
            render(title);
            return MENU_STATUS_SUCCESS;
        }
    }
    /* Hex A..F via UP/DOWN/LEFT/RIGHT/MENU/BACK is awkward — for simplicity
     * route them through HASH-prefixed sequences. Bench-only: this firmware
     * exposes a “hex helper” line at HASH which cycles A..F into the buffer. */
    if (s_mode == MODE_HEX && KEYCHECK_SHORTUP(ev->keys, KEY_HASH)) {
        if (s_len < MAX_HEX) {
            s_buf[s_len++] = 'A';
            s_buf[s_len]   = 0;
        }
        render(title);
        return MENU_STATUS_SUCCESS;
    }
    if (s_mode == MODE_HEX && KEYCHECK_LONGDOWN(ev->keys, KEY_HASH)) {
        if (s_len > 0) {
            char c = s_buf[s_len - 1];
            if (c >= 'A' && c < 'F') s_buf[s_len - 1] = c + 1;
            else if (c == 'F')       s_buf[s_len - 1] = 'A';
        }
        render(title);
        return MENU_STATUS_SUCCESS;
    }

    if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR)) {
        s_upperCase = !s_upperCase;
        commit_tap();
        render(title);
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT)) {
        if (s_len > 0) s_buf[--s_len] = 0;
        commit_tap();
        render(title);
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN)) {
        switch (s_mode) {
        case MODE_PASS:  keystore_set_passphrase(s_slot, s_buf, NULL);     break;
        case MODE_HEX:   (void)keystore_set_hex(s_slot, s_buf, NULL);      break;
        case MODE_LABEL: {
            key_slot_t mut;
            const key_slot_t *cur = keystore_get(s_slot);
            if (cur) {
                memcpy(&mut, cur, sizeof mut);
                strncpy(mut.label, s_buf, KEY_SLOT_LABEL_LEN);
                mut.label[KEY_SLOT_LABEL_LEN] = 0;
                /* push back via the appropriate setter */
                if (mut.mode == KEY_ENTRY_HEX) {
                    /* re-encode current key as hex string and re-set */
                    char hex[65];
                    static const char H[] = "0123456789ABCDEF";
                    for (int i = 0; i < KEY_SLOT_BYTES; ++i) {
                        hex[2*i]     = H[(mut.key[i] >> 4) & 0xF];
                        hex[2*i + 1] = H[ mut.key[i]       & 0xF];
                    }
                    hex[64] = 0;
                    keystore_set_hex(s_slot, hex, mut.label);
                } else if (mut.mode == KEY_ENTRY_PASSPHRASE) {
                    /* Can't recover the passphrase to re-derive; just rewrite
                     * label via direct write. */
                    extern void keystore_relabel(uint8_t slot1, const char *label);
                    keystore_relabel(s_slot, mut.label);
                }
            }
        } break;
        }
        keystore_save();
        memset(s_buf, 0, sizeof s_buf);
        s_len = 0;
        menuSystemPopPreviousMenu();
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED)) {
        memset(s_buf, 0, sizeof s_buf);
        s_len = 0;
        menuSystemPopPreviousMenu();
    }
    return MENU_STATUS_SUCCESS;
}

static menuStatus_t menu_pass(uiEvent_t *ev, bool first)
{ if (first) { s_len = 0; s_buf[0] = 0; commit_tap(); render("Passphrase (T9)"); return MENU_STATUS_SUCCESS; }
  return common_event(ev, "Passphrase (T9)"); }

static menuStatus_t menu_hex(uiEvent_t *ev, bool first)
{ if (first) { s_len = 0; s_buf[0] = 0; commit_tap(); render("Hex Key (64 hex)"); return MENU_STATUS_SUCCESS; }
  return common_event(ev, "Hex Key (64 hex)"); }

static menuStatus_t menu_label(uiEvent_t *ev, bool first)
{ if (first) {
        const key_slot_t *cur = keystore_get(s_slot);
        if (cur) { strncpy(s_buf, cur->label, KEY_SLOT_LABEL_LEN); s_len = (int)strlen(s_buf); }
        else     { s_buf[0] = 0; s_len = 0; }
        commit_tap();
        render("Label");
        return MENU_STATUS_SUCCESS;
  }
  return common_event(ev, "Label"); }

void menuKeyEntry_passphrase(uint8_t slot1)
{
    s_slot = slot1; s_mode = MODE_PASS;
    menuSystemPushNewMenu(MENU_KEY_ENTRY);
}
void menuKeyEntry_hex(uint8_t slot1)
{
    s_slot = slot1; s_mode = MODE_HEX;
    menuSystemPushNewMenu(MENU_KEY_ENTRY);
}
void menuKeyEntry_label(uint8_t slot1)
{
    s_slot = slot1; s_mode = MODE_LABEL;
    menuSystemPushNewMenu(MENU_KEY_ENTRY);
}

/*
 * The key-storage API doesn't expose a label-only update; provide it here.
 * Defined weak so it can be replaced if you choose to add the setter to
 * key_storage.c proper.
 */
__attribute__((weak)) void keystore_relabel(uint8_t slot1, const char *label)
{
    (void)slot1; (void)label;   /* no-op fallback */
}
