/*
 * Encryption Keys top-level menu — lists 16 slots, lets the user
 * open any slot for entry. Each slot displays its label and (for
 * passphrase-entry slots) the stored asterisk-pattern preview.
 *
 * Uses the standard menuDisplayTitle / menuDisplayEntry pattern so the
 * font size and theming match the rest of the menus.
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

static void updateScreen(void);
static void handleEvent(uiEvent_t *ev);

static menuStatus_t menuKeyManagementExitCode = MENU_STATUS_SUCCESS;

menuStatus_t menuKeyManagement(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		keystore_init();
		menuDataGlobal.numItems = KEY_SLOT_COUNT;
		if (menuDataGlobal.currentItemIndex < 0
		    || menuDataGlobal.currentItemIndex >= KEY_SLOT_COUNT)
		{
			menuDataGlobal.currentItemIndex = 0;
		}
		updateScreen();
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	menuKeyManagementExitCode = MENU_STATUS_SUCCESS;
	if (ev->hasEvent)
	{
		handleEvent(ev);
	}
	return menuKeyManagementExitCode;
}

static void updateScreen(void)
{
	displayClearBuf();
	menuDisplayTitle(currentLanguage->enc_keys);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(KEY_SLOT_COUNT, i);
		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		char buf[SCREEN_LINE_BUFFER_SIZE];
		const KeySlot_t *ks = keystore_get((uint8_t)(mNum + 1));

		if (ks && (ks->flags & KEY_FLAG_SET))
		{
			/* AES patch: nonce mode tag (Det/PTT) appended to the line. */
			char modeTag[5];
			switch (ks->nonceMode) {
				case NONCE_MODE_A_LC_STEAL:     strcpy(modeTag, " PTT"); break;
				default:                        strcpy(modeTag, " Det"); break;
			}
			/* AES patch: passphrase slots show the stored asterisk pattern
			 * (already in ***X***X form, no transformation needed); hex slots
			 * fall back to the label. */
			if (ks->entryMode == KEY_ENTRY_PASSPHRASE && ks->passPreview[0] != 0)
			{
				char preview[KEY_PASS_PREVIEW_LEN + 1] = {0};
				memcpy(preview, ks->passPreview, KEY_PASS_PREVIEW_LEN);
				snprintf(buf, sizeof(buf), "%2d:%s %s", mNum + 1, modeTag, preview);
			}
			else if (ks->label[0] != 0)
			{
				char lab[KEY_LABEL_LEN + 1] = {0};
				memcpy(lab, ks->label, KEY_LABEL_LEN);
				snprintf(buf, sizeof(buf), "%2d:%s %s", mNum + 1, modeTag, lab);
			}
			else
			{
				snprintf(buf, sizeof(buf), "%2d:%s <set>", mNum + 1, modeTag);
			}
		}
		else
		{
			snprintf(buf, sizeof(buf), "%2d: <empty>", mNum + 1);
		}
		menuDisplayEntry(i, mNum, buf, 0,
		                 THEME_ITEM_FG_MENU_ITEM,
		                 THEME_ITEM_COLOUR_NONE,
		                 THEME_ITEM_BG);
	}

	displayRender();
}

static void handleEvent(uiEvent_t *ev)
{
	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, KEY_SLOT_COUNT);
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, KEY_SLOT_COUNT);
		updateScreen();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuKeyEntry_passphrase((uint8_t)(menuDataGlobal.currentItemIndex + 1));
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH))
	{
		keystore_clear((uint8_t)(menuDataGlobal.currentItemIndex + 1));
		updateScreen();
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT) || KEYCHECK_PRESS(ev->keys, KEY_LEFT))
	{
		/* AES patch: cycle nonce mode (D <-> A) on populated slots.
		 * Option B was removed - only DETERMINISTIC and A_LC_STEAL remain. */
		uint8_t slot = (uint8_t)(menuDataGlobal.currentItemIndex + 1);
		KeySlot_t *ks = keystore_get_mut(slot);
		if (ks && (ks->flags & KEY_FLAG_SET)) {
			if (ks->nonceMode == NONCE_MODE_A_LC_STEAL) {
				ks->nonceMode = NONCE_MODE_DETERMINISTIC;
			} else {
				ks->nonceMode = NONCE_MODE_A_LC_STEAL;
			}
			keystore_save();
			updateScreen();
		}
	}
}
