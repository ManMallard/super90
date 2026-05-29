/*
 * Channel-mode Dual Watch screen (UI_DUAL_WATCH_CHANNEL)
 * ------------------------------------------------------
 *
 * Opened from the channel-mode quick menu (CH_SCREEN_QUICK_MENU_DUAL_WATCH).
 *
 * Layout — three rows of FONT_SIZE_3 text positioned to match the VFO dual
 * watch screen (channel A on the standard DISPLAY_Y_POS_RX_FREQ line, B on
 * DISPLAY_Y_POS_TX_FREQ), with a third zone-selection row sitting one row
 * (16 px) above A:
 *
 *   [Title bar: "Dual Watch"]
 *      ...
 *   > Z <zone name>            <- focus marker '>' on the focused row
 *    RA <channel A name>       <- 'R' marks the side currently keyed on RX
 *     B <channel B name>
 *   [B SCANNING badge if scanning]
 *
 * Controls
 *   STAR        cycle focus: Zone -> A -> B -> Zone -> ...
 *   LEFT/RIGHT  change the focused field by one step
 *   rotary +/-  alias for LEFT / RIGHT
 *   long UP     start B-channel scan (sweep backward through zone)
 *   long DOWN   start B-channel scan (sweep forward through zone)
 *   RED         if B is scanning: stop, leave B on the current channel
 *               otherwise: exit dual watch, leave the radio on whichever
 *               side was last keyed
 *
 * Timing — single period, DW_SWAP_MS (135 ms), drives both the A<->B swap
 * AND the B-scan step.  When scanning, B's index is advanced AT THE
 * MOMENT of the A→B swap, so each scanned channel gets exactly one full
 * B-keying window (135 ms) of listening before the scan moves on.
 * (The previous independent 45 ms scan tick caused B's index to walk 3x
 * faster than the user could possibly hear, defeating the point.)
 * A carrier on the currently-keyed side pauses the swap so an incoming
 * call isn't cut off mid-syllable.
 *
 * Zone handling — supports both real zones and the codeplug's "All
 * Channels" virtual zone, which has channels[] zeroed and is iterated
 * over 1..highestIndex via codeplugAllChannelsIndexIsInUse().  The index
 * semantic switches based on which kind of zone is loaded:
 *   real zone: 0..NOT_IN_CODEPLUGDATA_numChannelsInZone - 1, indexing
 *              s_zone.channels[].
 *   all-channels: 1..NOT_IN_CODEPLUGDATA_highestIndex, the absolute
 *                 codeplug channel number itself.
 * The dw*Idx helpers below abstract over both.
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

#define DW_SWAP_MS    135u  /* A<->B alternation period; also B-scan step
                             * period when scanning (one swap -> one new
                             * scanned channel keyed for one window). */

/* s_zone is 188 B (name + channels[80] + 3 counts).  Kept in CCMRAM
 * because the lazy-init pattern guarantees write-before-read: every
 * code path that reads s_zone.* has been preceded by a call to
 * codeplugZoneGetDataForNumber(&s_zone, ...) which fills the entire
 * struct.  Same safety story as the other CCM scratch consumers
 * (melody_generic, ambeData, screen notification buffer, GPS waypoint
 * bank). */
static __attribute__((section(".ccmram"))) CodeplugZone_t s_zone;

static int16_t   s_zoneIdx;
static int16_t   s_chAIdx;    /* zone-relative for real zones, absolute channel# for all-channels */
static int16_t   s_chBIdx;
static DwFocus_t s_focus;
static uint8_t   s_activeAB;  /* 0 = A keyed, 1 = B keyed */
static uint32_t  s_lastSwapMs;
static bool      s_scanningB;
static int8_t    s_scanDir;   /* +1 forward, -1 backward */
static int16_t   s_originalChannelNum;  /* absolute number we were on at entry */

/* ---------------------------------------------------------------------- */
/* Zone-agnostic index helpers                                             */
/* ---------------------------------------------------------------------- */

static bool dwZoneIsAll(void)
{
	return CODEPLUG_ZONE_IS_ALLCHANNELS(s_zone);
}

/* Map our local index (which has different semantics in real vs
 * all-channels zones) to the absolute codeplug channel number. */
static uint16_t dwIdxToChannelNum(int16_t idx)
{
	if (dwZoneIsAll())
	{
		return (uint16_t)idx;
	}
	if ((idx < 0) || (idx >= s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone))
	{
		return 0;
	}
	return s_zone.channels[idx];
}

/* Walk to the next/prev valid index in the current zone.  delta = +1 or -1.
 * For all-channels, skips indices that aren't in use by the codeplug. */
static int16_t dwAdvanceIdx(int16_t idx, int delta)
{
	if (dwZoneIsAll())
	{
		int high = s_zone.NOT_IN_CODEPLUGDATA_highestIndex;
		if (high < 1) return 1;
		/* Bounded loop — walk at most one full pass.  Without the
		 * bound, a codeplug with zero in-use channels would spin
		 * forever. */
		for (int tries = 0; tries <= high; tries++)
		{
			if (delta > 0)
			{
				idx = (int16_t)((idx % high) + 1);
			}
			else
			{
				idx = (int16_t)((((idx - 1) + high - 1) % high) + 1);
			}
			if (codeplugAllChannelsIndexIsInUse(idx))
			{
				return idx;
			}
		}
		return idx;
	}

	int cnt = s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone;
	if (cnt <= 0) return 0;
	return (int16_t)(((int)idx + delta + cnt) % cnt);
}

/* Clamp an index into the valid range for the current zone. */
static int16_t dwClampIdx(int16_t idx)
{
	if (dwZoneIsAll())
	{
		int high = s_zone.NOT_IN_CODEPLUGDATA_highestIndex;
		if (high < 1) return 1;
		if (idx < 1) idx = 1;
		if (idx > high) idx = (int16_t)high;
		if (!codeplugAllChannelsIndexIsInUse(idx))
		{
			return dwAdvanceIdx(idx, 1);
		}
		return idx;
	}

	int cnt = s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone;
	if (cnt <= 0) return 0;
	if (idx < 0) idx = 0;
	if (idx >= cnt) idx = (int16_t)(cnt - 1);
	return idx;
}

/* Default starting index for a freshly-loaded zone. */
static int16_t dwInitialIdx(void)
{
	return dwZoneIsAll() ? (int16_t)1 : (int16_t)0;
}

/* Find the index that maps to a given absolute channel number.  For
 * all-channels that's just the channel number itself; for real zones it
 * searches channels[].  Returns dwInitialIdx() if not found. */
static int16_t dwFindIdxForChannelNum(uint16_t chNum)
{
	if (dwZoneIsAll())
	{
		return (int16_t)chNum;
	}
	for (int i = 0; i < s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone; i++)
	{
		if (s_zone.channels[i] == chNum)
		{
			return (int16_t)i;
		}
	}
	return dwInitialIdx();
}

/* ---------------------------------------------------------------------- */
/* Channel helpers                                                         */
/* ---------------------------------------------------------------------- */

static void getChanName(int16_t idx, char *out)
{
	out[0] = '\0';
	uint16_t chNum = dwIdxToChannelNum(idx);
	if (chNum == 0)
	{
		snprintf(out, 17, "---");
		return;
	}
	CodeplugChannel_t tmp;
	codeplugChannelGetDataForIndex(chNum, &tmp);
	codeplugUtilConvertBufToString(tmp.name, out, 16);
	out[16] = '\0';
}

/* Program the radio for the given local index. */
static void loadChannelByIdx(int16_t idx)
{
	uint16_t chNum = dwIdxToChannelNum(idx);
	if (chNum == 0) return;

	codeplugChannelGetDataForIndex(chNum, &channelScreenChannelData);
	currentChannelData = &channelScreenChannelData;

	trxRxAndTxOff(true);
	trxSetModeAndBandwidth(currentChannelData->chMode,
	                       (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
	trxSetFrequency(currentChannelData->rxFreq,
	                currentChannelData->txFreq, DMR_MODE_AUTO);
	trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
	trxRxOn(true);
}

/* ---------------------------------------------------------------------- */
/* Zone reload + clamp                                                     */
/* ---------------------------------------------------------------------- */

static void reloadZone(int16_t newZoneIdx)
{
	s_zoneIdx = newZoneIdx;
	codeplugZoneGetDataForNumber(s_zoneIdx, &s_zone);
	/* Clamp the existing indices into the new zone, falling back to
	 * the zone's initial index when the previous value is meaningless
	 * for the new zone type. */
	s_chAIdx = dwClampIdx(s_chAIdx);
	s_chBIdx = dwClampIdx(s_chBIdx);
	/* If A and B happened to clamp to the same index in a >1-channel
	 * zone, bump B to the next valid one for a sensible default. */
	int cnt = dwZoneIsAll() ? s_zone.NOT_IN_CODEPLUGDATA_highestIndex
	                        : s_zone.NOT_IN_CODEPLUGDATA_numChannelsInZone;
	if ((s_chAIdx == s_chBIdx) && (cnt > 1))
	{
		s_chBIdx = dwAdvanceIdx(s_chBIdx, 1);
	}
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
	getChanName(s_chAIdx, chAName);
	getChanName(s_chBIdx, chBName);

	/* Position A and B exactly where VFO dual watch puts its two
	 * frequency rows; zone sits one row above A.  These macros expand
	 * to platform-correct y coordinates (see uiGlobals.h). */
	const int16_t yPositions[3] = {
		(int16_t)(DISPLAY_Y_POS_RX_FREQ - 16),
		(int16_t)DISPLAY_Y_POS_RX_FREQ,
		(int16_t)DISPLAY_Y_POS_TX_FREQ
	};
	const char letters[3] = { 'Z', 'A', 'B' };
	const char *names[3]  = { zoneName, chAName, chBName };
	char line[24];

	for (int i = 0; i < 3; i++)
	{
		bool focused  = (i == (int)s_focus);
		/* Only A/B rows can be "currently keyed" (the radio side mark). */
		bool isActive = (i > 0) && (((int)s_activeAB) == (i - 1));
		snprintf(line, sizeof(line), "%c%c%c %s",
		         (focused  ? '>' : ' '),
		         (isActive ? 'R' : ' '),
		         letters[i],
		         names[i]);
		displayPrintAt(0, yPositions[i], line, FONT_SIZE_3);
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

		s_chAIdx = dwFindIdxForChannelNum((uint16_t)s_originalChannelNum);
		s_chAIdx = dwClampIdx(s_chAIdx);
		s_chBIdx = dwAdvanceIdx(s_chAIdx, 1);

		s_focus       = FOCUS_ZONE;
		s_activeAB    = 0;
		s_scanningB   = false;
		s_scanDir     = 1;
		s_lastSwapMs  = ticksGetMillis();

		loadChannelByIdx(s_chAIdx);

		voicePromptsInit();
		voicePromptsAppendLanguageString(currentLanguage->dual_watch);
		voicePromptsPlay();

		render();
		return MENU_STATUS_SUCCESS;
	}

	uint32_t now = ticksGetMillis();

	/* Unified swap-and-scan tick: every DW_SWAP_MS, swap which side is
	 * keyed.  When scanning B, advance B's index AT THE A→B swap so
	 * each scanned channel gets one full B-keying window of listening.
	 * Carrier on the currently-keyed side pauses everything so the
	 * user can actually hear an incoming call. */
	if (((now - s_lastSwapMs) >= DW_SWAP_MS) &&
	    !trxCarrierDetected(RADIO_DEVICE_PRIMARY))
	{
		s_lastSwapMs = now;
		bool aToB = (s_activeAB == 0);
		s_activeAB ^= 1u;
		if (aToB && s_scanningB)
		{
			/* We're about to key B — step the scan first so the
			 * channel we key is the next one in the sweep. */
			s_chBIdx = dwAdvanceIdx(s_chBIdx, s_scanDir);
		}
		loadChannelByIdx((s_activeAB == 0) ? s_chAIdx : s_chBIdx);
		render();
	}

	if (!ev->hasEvent) return MENU_STATUS_SUCCESS;

	/* RED: stop scan if scanning, otherwise exit. */
	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (s_scanningB)
		{
			s_scanningB = false;
			render();
			return MENU_STATUS_SUCCESS;
		}
		/* Leave the radio on whichever side was last keyed. */
		uint16_t activeChNum = dwIdxToChannelNum(
		    (s_activeAB == 0) ? s_chAIdx : s_chBIdx);
		uiDataGlobal.currentSelectedChannelNumber = (activeChNum != 0)
		    ? activeChNum
		    : s_originalChannelNum;
		/* Force the channel screen to reload on next entry. */
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

	/* Long UP / DOWN with B focused starts the B scan. */
	if (s_focus == FOCUS_CHB)
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_UP))
		{
			s_scanningB = true;
			s_scanDir   = -1;
			render();
			return MENU_STATUS_SUCCESS;
		}
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_DOWN))
		{
			s_scanningB = true;
			s_scanDir   = 1;
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
				loadChannelByIdx((s_activeAB == 0) ? s_chAIdx : s_chBIdx);
			}
		}
		else
		{
			int16_t *target = (s_focus == FOCUS_CHA) ? &s_chAIdx : &s_chBIdx;
			*target = dwAdvanceIdx(*target, delta);
			if (((s_focus == FOCUS_CHA) && (s_activeAB == 0)) ||
			    ((s_focus == FOCUS_CHB) && (s_activeAB == 1)))
			{
				loadChannelByIdx(*target);
			}
		}
		render();
	}

	return MENU_STATUS_SUCCESS;
}
