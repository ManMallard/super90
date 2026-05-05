/*
 * menuKeyManagement.c — top-level menu for the 16-slot AES key bank.
 *
 * UI surface:
 *   Up/Down  — move cursor between slots
 *   Green    — open slot (sub-menu: enter passphrase / enter hex / clear / set label)
 *   Red      — exit and persist
 *
 * Note: this targets the OpenGD77 menu/UI conventions. If symbol names differ
 * in your tree (uiUtilityRender*, ucPrintAt, etc.) adjust at the marked lines.
 */
#include <string.h>
#include <stdio.h>
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "functions/settings.h"
#include "crypto/key_storage.h"

static int  s_cursor;
static bool s_editing_label;

/* Forward declarations from menuKeyEntry.c */
extern void menuKeyEntry_passphrase(uint8_t slot1);
extern void menuKeyEntry_hex(uint8_t slot1);
extern void menuKeyEntry_label(uint8_t slot1);

static void render(void)
{
    char line[24];
    ucClearBuf();
    ucPrintCentered(0, "Encryption Keys", FONT_SIZE_3);

    int top = (s_cursor < 4) ? 0 : (s_cursor - 3);
    for (int row = 0; row < 4; ++row) {
        int slot = top + row;        /* 0..15 → slot1 = slot+1 */
        if (slot >= KEY_SLOT_COUNT) break;

        const key_slot_t *ks = keystore_get(slot + 1);
        if (ks == NULL) {
            snprintf(line, sizeof line, "%2d: <empty>", slot + 1);
        } else {
            const char *tag = (ks->mode == KEY_ENTRY_PASSPHRASE) ? "P" :
                              (ks->mode == KEY_ENTRY_HEX)        ? "H" : "?";
            snprintf(line, sizeof line, "%2d:%s %s", slot + 1, tag,
                     ks->label[0] ? ks->label : "(unnamed)");
        }
        if (slot == s_cursor) {
            ucFillRect(0, 16 + row * 12, 128, 12, false);
            ucPrintAt(2, 17 + row * 12, line, FONT_SIZE_2);
        } else {
            ucPrintAt(2, 17 + row * 12, line, FONT_SIZE_2);
        }
    }
    ucRender();
}

menuStatus_t menuKeyManagement(uiEvent_t *ev, bool isFirstRun)
{
    if (isFirstRun) {
        keystore_load();
        s_cursor = 0;
        s_editing_label = false;
        render();
        return MENU_STATUS_SUCCESS;
    }
    if ((ev->events & KEY_EVENT) == 0) return MENU_STATUS_SUCCESS;

    if (KEYCHECK_PRESS(ev->keys, KEY_DOWN)) {
        if (s_cursor < KEY_SLOT_COUNT - 1) s_cursor++;
        render();
    } else if (KEYCHECK_PRESS(ev->keys, KEY_UP)) {
        if (s_cursor > 0) s_cursor--;
        render();
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN)) {
        /* Sub-menu: 1=passphrase, 2=hex, 3=label, 4=clear, RED=back. */
        menuKeyEntry_passphrase(s_cursor + 1);   /* default to passphrase */
        render();
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH)) {
        menuKeyEntry_hex(s_cursor + 1);
        render();
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR)) {
        menuKeyEntry_label(s_cursor + 1);
        render();
    } else if (KEYCHECK_LONGDOWN(ev->keys, KEY_LEFT)) {
        keystore_clear_slot(s_cursor + 1);
        keystore_save();
        render();
    } else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED)) {
        keystore_save();
        menuSystemPopPreviousMenu();
        return MENU_STATUS_SUCCESS;
    }

    return MENU_STATUS_SUCCESS;
}
