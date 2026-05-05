/*
 * Encryption Keys top-level menu — lists 16 slots, lets the user
 * open any slot for entry. Each slot displays its label or "<empty>".
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
		if (ks && (ks->flags & KEY_FLAG_SET) && ks->label[0])
		{
			char lab[KEY_LABEL_LEN + 1] = {0};
			memcpy(lab, ks->label, KEY_LABEL_LEN);
			snprintf(buf, sizeof(buf), "%2d: %s", mNum + 1, lab);
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
}
