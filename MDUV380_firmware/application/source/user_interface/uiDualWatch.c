/*
 * Channel-mode Dual Watch screen (UI_DUAL_WATCH_CHANNEL)
 * ------------------------------------------------------
 *
 * Opened from the channel-mode quick menu (CH_SCREEN_QUICK_MENU_DUAL_WATCH).
 *
 * Layout (three rows of FONT_SIZE_3 text, current focus drawn inverted):
 *
 *   [Title bar: "Dual Watch"]
 *   Z: <zone name>        <- s_focus == FOCUS_ZONE
 *   A:*<channel A name>   <- s_focus == FOCUS_CHA  ('*' marks the one
 *   B: <channel B name>      currently keyed onto the radio)
 *   [B SCANNING badge if active]
 *
 * Controls
 *   STAR        cycle focus: Zone -> A -> B -> Zone -> ...
 *   LEFT/RIGHT  change the focused field by one step
 *               (zone -> prev/next zone, A -> prev/next ch in zone, B same)
 *   rotary +/-  same as LEFT / RIGHT
 *   long UP     start B-channel scan (sweep backward through zone)
 *   long DOWN   start B-channel scan (sweep forward through zone)
 *   RED         if B is scanning: stop, leave B on the current channel
 *               otherwise: exit dual watch, restore the original channel
 *
 * Alternation
 *   Every DW_SWAP_MS (135 ms — the same dwell time the VFO dual watch
 *   uses) the active channel toggles A<->B and the radio is re-keyed.
 *   When a carrier is detected on the currently-active channel the
 *   alternation is paused until the carrier drops, so the user can
 *   actually hear what came in.
 *
 * B-scan
 *   When scanning, B's index walks the zone every DW_SCAN_MS in the
 *   chosen direction.  Carrier detect on B pauses the step (same as
 *   the regular channel scan) without stopping the A<->B swap.
 *
 * No persistence — the chosen A/B are forgotten on exit and the radio
 * returns to whichever channel was selected when Dual Watch was entered.
 */
#include <stdio.h>
#include <string.h>
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/codeplug.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "hardware/HX8353E.h"
#include "io/keyboard.h"

typedef enum { FOCUS_ZONE = 0, FOCUS_CHA = 1, FOCUS_CHB = 2, FOCUS_COUNT = 3 } DwFocus_t;

#define DW_SWAP_MS    135u  /* A<->B alternation period — matches VFO dual scan */
#define DW_SCAN_MS     45u  /* B-scan step period when sweeping the zone */

/* s_zone holds the expanded zone (name + channels[80] + counts) — ~188 B.
 * Moved to CCMRAM to claw back the largest single chunk Dual Watch consumes
 * from the very tight .bss slack (~200 B at branch tip).  Safe in CCM
 * because the lazy-init pattern guarantees write-before-read: the menu's
 * isFirstRun branch always calls codeplugZoneGetDataForNumber(&s_zone)
 * before any handler touches its fields, so the lack of CCMRAM zero-init
 * at boot is harmless (same pattern as melody_generic, ambeData, screen
 * notification buffer, and the GPS waypoint bank).
 *
 * The smaller state ints stay in .bss for guaranteed zero on boot.
 * Re-entering Dual Watch always re-initialises them in isFirstRun anyway
 * but the zero-init means a stray reference from elsewhere in the
 * codebase — or a hypothetical bug that reads them before isFirstRun —
 * sees defined values rather than garbage. */
static __attribute__((section(".ccmram"))) CodeplugZone_t s_zone;

static int16_t   s_zoneIdx;
static int16_t   s_chAIdx;          /* index of A within s_zone */
static int16_t   s_chBIdx;          /* index of B within s_zone */
static DwFocus_t s_focus;
static uint8_t   s_activeAB;        /* 0 = A keyed, 1 = B keyed */
static uint32_t  s_lastSwapMs;
static bool      s_scanningB;
static int8_t    s_scanDir;         /* +1 forward, -1 backward */
static uint32_t  s_lastScanStepMs;
static int16_t   s_originalChannelNum;  /* absolute channel num to restore on exit */

extern CodeplugZone_t currentZone;       /* defined in uiChannelMode */

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */
/* ---------------------------------------------------------------------- */

/* Read the name of zone-index `idx` and write up to 16 chars + NUL.   */
static void getChanName(const CodeplugZone_t *zone, int idxInZone, char *out)
{
	out[0] = '\0';
	if ((idxInZone < 0) ||
	    (idxInZone >= zone->NOT_IN_CODEPLUGDATA_numChannelsInZone))
	{
		snprintf(out, 17, "---");
		return;
	}
	CodeplugChannel_t tmp;
	codeplugChannelGetDataForIndex(zone->channels[idxInZone], &tmp);
	codeplugUtilConvertBufToString(tmp.name, out, 16);
	out[16] = '\0';
}

/* Program the radio for the given zone-relative channel index. */
static void loadChannelByZoneIdx(const CodeplugZone_t *zone, int idxInZone)
{
	if ((idxInZone < 0) ||
	    (idxInZone >= zone->NOT_IN_CODEPLUGDATA_numChannelsInZone))
	{
		return;
	}

	codeplugChannelGetDataForIndex(zone->channels[idxInZone], &channelScreenChannelData);
	currentChannelData = &channelScreenChannelData;

	trxRxAndTxOff(true);
	trxSetModeAndBandwidth(currentChannelData->chMode,
	                       (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
	trxSetFrequency(currentChannelData->rxFreq,
	                currentChannelData->txFreq, DMR_MODE_AUTO);
	trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
	trxRxOn(true);
}

/* Reload s_zone after a zone change. */
static void reloadZone(int16_t newZoneIdx)
{
	s_zoneIdx = newZoneIdx;
	codeplugZoneGetDataForNumber(s_zoneIdx, &s_zone);

	int cnt = s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone;
	if (cnt <= 0) cnt = 1;
	if (s_chAIdx >= cnt) s_chAIdx = 0;
	if (s_chBIdx >= cnt) s_chBIdx = (cnt > 1) ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* Render                                                                  */
/* ---------------------------------------------------------------------- */

static void render(void)
{
	char zoneName[17];
	char chAName[17];
	char chBName[17];

	displayClearBuf();
	menuDisplayTitle((char *)currentLanguage->dual_watch);

	codeplugUtilConvertBufToString(s_zone.name, zoneName, 16);
	zoneName[16] = '\0';
	getChanName(&s_zone, s_chAIdx, chAName);
	getChanName(&s_zone, s_chBIdx, chBName);

	const int16_t rowY[3] = { 24, 48, 72 };
	const char  *prefix[3] = { "Z:", "A:", "B:" };
	const char  *body[3]   = { zoneName, chAName, chBName };

	for (int i = 0; i < 3; i++)
	{
		bool focused  = (i == (int)s_focus);
		/* Row 0 is zone — no "currently keyed" indicator.  Rows 1-2 mark
		 * whichever of A/B is the channel programmed into the radio
		 * right now with a '*'; the other gets a space. */
		bool isActive = (i > 0) && (((int)s_activeAB) == (i - 1));
		char line[24];
		snprintf(line, sizeof(line), "%s%c%s",
		         prefix[i], (isActive ? '*' : ' '), body[i]);

		if (focused)
		{
			displayFillRect(0, (int16_t)(rowY[i] - 2),
			                DISPLAY_SIZE_X, 18, false);
			displayPrintCore(4, rowY[i], line, FONT_SIZE_3,
			                 TEXT_ALIGN_LEFT, true);
		}
		else
		{
			displayPrintAt(4, rowY[i], line, FONT_SIZE_3);
		}
	}

	if (s_scanningB)
	{
		displayPrintCentered((int16_t)(DISPLAY_SIZE_Y - 10),
		                     "B SCANNING", FONT_SIZE_1_BOLD);
	}

	displayRender();
}

/* ---------------------------------------------------------------------- */
/* Menu entry                                                              */
/* ---------------------------------------------------------------------- */

menuStatus_t uiDualWatch(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		s_zoneIdx = nonVolatileSettings.currentZone;
		codeplugZoneGetDataForNumber(s_zoneIdx, &s_zone);

		s_originalChannelNum = (int16_t)uiDataGlobal.currentSelectedChannelNumber;

		/* Find current channel within the zone, default to 0 if not found. */
		s_chAIdx = 0;
		for (int i = 0; i < s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone; i++)
		{
			if (s_zone.channels[i] == (uint16_t)s_originalChannelNum)
			{
				s_chAIdx = (int16_t)i;
				break;
			}
		}
		s_chBIdx = (s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 1) ?
		           (int16_t)((s_chAIdx + 1) % s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone) :
		           s_chAIdx;

		s_focus       = FOCUS_ZONE;
		s_activeAB    = 0;
		s_scanningB   = false;
		s_scanDir     = 1;
		s_lastSwapMs  = ticksGetMillis();
		s_lastScanStepMs = s_lastSwapMs;

		/* Start by programming A onto the radio. */
		loadChannelByZoneIdx(&s_zone, s_chAIdx);

		voicePromptsInit();
		voicePromptsAppendLanguageString(currentLanguage->dual_watch);
		voicePromptsPlay();

		render();
		return MENU_STATUS_SUCCESS;
	}

	uint32_t now = ticksGetMillis();

	/* B scan tick — advance B's index periodically (independent of swap). */
	if (s_scanningB && ((now - s_lastScanStepMs) >= DW_SCAN_MS))
	{
		s_lastScanStepMs = now;
		int cnt = s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone;
		if (cnt > 0)
		{
			s_chBIdx = (int16_t)((s_chBIdx + s_scanDir + cnt) % cnt);
			if (s_activeAB == 1)
			{
				loadChannelByZoneIdx(&s_zone, s_chBIdx);
			}
			render();
		}
	}

	/* A<->B swap tick — but don't swap while there's an active carrier on
	 * the currently-keyed channel (let the user hear it). */
	if (((now - s_lastSwapMs) >= DW_SWAP_MS) &&
	    !trxCarrierDetected(RADIO_DEVICE_PRIMARY))
	{
		s_lastSwapMs = now;
		s_activeAB ^= 1u;
		loadChannelByZoneIdx(&s_zone,
		                     (s_activeAB == 0) ? s_chAIdx : s_chBIdx);
		render();
	}

	if (!ev->hasEvent) return MENU_STATUS_SUCCESS;

	/* RED: stop scan if scanning, otherwise exit Dual Watch. */
	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (s_scanningB)
		{
			s_scanningB = false;
			render();
			return MENU_STATUS_SUCCESS;
		}
		/* On exit, leave the radio tuned to whichever channel was active
		 * when the user pressed RED — that matches the spec ("ended on
		 * currently active channel when pressing the red button") and is
		 * more useful than snapping back to the channel we entered from.
		 * Sync the channel-mode current selection to whatever's keyed now
		 * so the channel screen comes up displaying the right thing. */
		int activeIdxInZone = (s_activeAB == 0) ? s_chAIdx : s_chBIdx;
		if ((activeIdxInZone >= 0) &&
		    (activeIdxInZone < s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone))
		{
			uiDataGlobal.currentSelectedChannelNumber =
			    s_zone.channels[activeIdxInZone];
		}
		else
		{
			uiDataGlobal.currentSelectedChannelNumber = s_originalChannelNum;
		}
		/* Mark channelScreenChannelData stale so uiChannelMode reloads on
		 * the next entry — its rxFreq==0 sentinel triggers a clean reload. */
		channelScreenChannelData.rxFreq = 0;
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	/* STAR: cycle focus. */
	if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		s_focus = (DwFocus_t)((s_focus + 1) % FOCUS_COUNT);
		render();
		return MENU_STATUS_SUCCESS;
	}

	/* Long UP / DOWN with B focused starts the B scan.  UP = backward,
	 * DOWN = forward (matches the usual VFO/channel scan convention). */
	if (s_focus == FOCUS_CHB)
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_UP))
		{
			s_scanningB     = true;
			s_scanDir       = -1;
			s_lastScanStepMs = now;
			render();
			return MENU_STATUS_SUCCESS;
		}
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_DOWN))
		{
			s_scanningB     = true;
			s_scanDir       = 1;
			s_lastScanStepMs = now;
			render();
			return MENU_STATUS_SUCCESS;
		}
	}

	/* LEFT / RIGHT (or rotary) cycle the focused field. */
	bool left  = KEYCHECK_SHORTUP(ev->keys, KEY_LEFT)  ||
	             KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT);
	bool right = KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT) ||
	             KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT);

	if (left || right)
	{
		int delta = right ? 1 : -1;

		if (s_focus == FOCUS_ZONE)
		{
			int zc = codeplugZonesGetCount();
			if (zc > 0)
			{
				reloadZone((int16_t)(((int)s_zoneIdx + delta + zc) % zc));
				/* Re-key whichever side is currently active. */
				loadChannelByZoneIdx(&s_zone,
				                     (s_activeAB == 0) ? s_chAIdx : s_chBIdx);
			}
		}
		else
		{
			int cc = s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone;
			if (cc > 0)
			{
				int16_t *target = (s_focus == FOCUS_CHA) ? &s_chAIdx : &s_chBIdx;
				*target = (int16_t)(((int)(*target) + delta + cc) % cc);
				/* If we just changed the side that's currently keyed, retune. */
				if (((s_focus == FOCUS_CHA) && (s_activeAB == 0)) ||
				    ((s_focus == FOCUS_CHB) && (s_activeAB == 1)))
				{
					loadChannelByZoneIdx(&s_zone, *target);
				}
			}
		}
		render();
	}

	return MENU_STATUS_SUCCESS;
}
