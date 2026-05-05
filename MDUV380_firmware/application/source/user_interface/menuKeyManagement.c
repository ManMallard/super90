/*
 * Encryption Keys top-level menu — lists 16 slots, lets the user
 * open any slot for entry. Each slot displays its label or "<empty>".
 */
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "io/keyboard.h"
#include "hardware/HX8353E.h"

#include "crypto/key_storage.h"

extern void menuKeyEntry_passphrase(uint8_t slot1based);

static int s_cursor = 0;

static void render(void)
{
    char line[20];
    displayClearBuf();
    displayPrintCentered(0, "Encryption Keys", FONT_SIZE_2);

    int rowsToShow = 5;
    int top = s_cursor - 2;
    if (top < 0) top = 0;
    if (top > KEY_SLOT_COUNT - rowsToShow) top = KEY_SLOT_COUNT - rowsToShow;

    for (int i = 0; i < rowsToShow; ++i) {
        int slot = top + i;
        if (slot >= KEY_SLOT_COUNT) break;
        const KeySlot_t *ks = keystore_get((uint8_t)(slot + 1));
        if (ks && (ks->flags & KEY_FLAG_SET) && ks->label[0]) {
            char tmp[KEY_LABEL_LEN + 1] = {0};
            memcpy(tmp, ks->label, KEY_LABEL_LEN);
            snprintf(line, sizeof(line), "%2d: %s", slot + 1, tmp);
        } else {
            snprintf(line, sizeof(line), "%2d: <empty>", slot + 1);
        }
        bool sel = (slot == s_cursor);
        if (sel) displayFillRect(0, 16 + (i * 10), 132, 10, false);
        displayPrintAt(2, 16 + (i * 10), line, FONT_SIZE_1);
    }
    displayRender();
}

menuStatus_t menuKeyManagement(uiEvent_t *ev, bool isFirstRun)
{
    if (isFirstRun) {
        keystore_init();
        s_cursor = 0;
        render();
        return MENU_STATUS_SUCCESS;
    }
    if (ev->hasEvent) {
        if (KEYCHECK_PRESS(ev->keys, KEY_DOWN)) {
            if (s_cursor < KEY_SLOT_COUNT - 1) s_cursor++;
            render();
        } else if (KEYCHECK_PRESS(ev->keys, KEY_UP)) {
            if (s_cursor > 0) s_cursor--;
            render();
        } else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN)) {
            menuKeyEntry_passphrase((uint8_t)(s_cursor + 1));
            return MENU_STATUS_SUCCESS;
        } else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED)) {
            menuSystemPopPreviousMenu();
            return MENU_STATUS_SUCCESS;
        } else if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH)) {
            keystore_clear((uint8_t)(s_cursor + 1));
            render();
        }
    }
    return MENU_STATUS_SUCCESS;
}
