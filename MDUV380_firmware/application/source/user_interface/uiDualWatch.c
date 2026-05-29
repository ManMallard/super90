/*
 * Channel-mode Dual Watch screen (UI_DUAL_WATCH_CHANNEL)
 * ------------------------------------------------------
 *
 * Opened from the channel-mode quick menu (CH_SCREEN_QUICK_MENU_DUAL_WATCH).
 *
 * Layout — three rows of FONT_SIZE_3 text positioned to match the VFO dual
 * watch screen (A on DISPLAY_Y_POS_RX_FREQ, B on DISPLAY_Y_POS_TX_FREQ),
 * with a third zone-selection row sitting one row (16 px) above A:
 *
 *   [Title bar: "Dual Watch"]
 *      ...
 *   > Z <zone name>            <- focus marker '>' on the focused row
 *    RA <channel A name>       <- 'R' marks the side currently keyed on RX
 *     B <channel B name>
 *   [B SCANNING badge if scanning]
 *
 * Controls
 *   UP / DOWN     change the focused field by +1 / -1
 *   rotary +/-    alias for UP / DOWN
 *   STAR (short)  cycle focus: Zone -> A -> B -> Zone -> ...
 *   STAR (long)   focus B and start the B-channel scan (forward sweep)
 *   RED           if B is scanning: stop, leave B on the current channel
 *                 otherwise:        exit Dual Watch, leave the radio on
 *                                   whichever channel the focus points to
 *   PTT           transmit on the CURRENTLY-FOCUSED channel — Dual Watch
 *                 swaps which side the AT1846 hardware is listening to
 *                 every DW_SWAP_MS, but the focused channel is what gets
 *                 keyed up.  Zone-focused PTT falls back to whichever
 *                 side was active at the moment PTT was pressed.
 *
 * Timing — single period DW_SWAP_MS drives both A<->B alternation AND the
 * B-scan step.  Set to 80 ms:
 *   - DMR slot is 30 ms; 80 ms = 2.67 slots — usually catches at least
 *     one full slot of carrier before moving on.
 *   - M17 frame is 40 ms; 80 ms = 2 frames — comfortable margin.
 *   - Analog RSSI settles in < 20 ms, so plenty.
 *   If a DMR signal occasionally gets missed because the slot phase
 *   aligns badly with the dwell window, bump DW_SWAP_MS to 100 ms.
 *
 * Auto-jump on B-scan carrier — when B is scanning and a carrier is
 * detected while B is the keyed side, focus jumps to B and the scan
 * stops.  This way the very next PTT transmits on the channel the
 * operator just heard activity on, instead of whatever B was about to
 * sweep to next.
 *
 * Focus preservation across PTT — when the user PTTs, applicationMain
 * pushes UI_TX_SCREEN; on PTT release UI_TX_SCREEN pops and our menu
 * is re-entered with isFirstRun=true.  Without protection that would
 * reset focus to Zone and forget A/B.  A static s_initialized flag
 * (.bss → guaranteed zero at first-ever entry, persists across PTT)
 * gates the full reset: it's only run on a genuinely fresh entry from
 * the quick menu.  RED-exit clears the flag so the next open is fresh.
 *
 * Zone handling — supports both real zones and the codeplug's "All
 * Channels" virtual zone (channels[] zeroed, iterated 1..highestIndex
 * via codeplugAllChannelsIndexIsInUse()).  The dw*Idx helpers below
 * abstract over both, so the index value means different things
 * depending on dwZoneIsAll() but every caller is unaware.
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

#define DW_SWAP_MS    80u  /* Alternation + scan step.  See header
                            * comment for the DMR/M17 timing rationale. */

/* s_zone (188 B) and s_focusedData (56 B) are in CCMRAM.  Both follow
 * the write-before-read pattern that keeps CCM safe in this build:
 * every code path that reads from them is preceded by a write within
 * the same uiDualWatch() invocation. */
static __attribute__((section(".ccmram"))) CodeplugZone_t    s_zone;

static int16_t   s_zoneIdx;
static int16_t   s_chAIdx;    /* zone-relative for real zones, absolute channel# for all-channels */
static int16_t   s_chBIdx;
static DwFocus_t s_focus;
static uint8_t   s_activeAB;  /* 0 = A keyed, 1 = B keyed (which side the hardware is on now) */
static uint32_t  s_lastSwapMs;
static bool      s_scanningB;
static int8_t    s_scanDir;   /* +1 forward, -1 backward */
static int16_t   s_originalChannelNum;  /* absolute # we were on at entry */
static bool      s_initialized;         /* false until full-init runs; survives PTT round-trip */

/* ---------------------------------------------------------------------- */
/* Zone-agnostic index helpers                                             */
/* ---------------------------------------------------------------------- */

static bool dwZoneIsAll(void)
{
	return CODEPLUG_ZONE_IS_ALLCHANNELS(s_zone);
}

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

static int16_t dwAdvanceIdx(int16_t idx, int delta)
{
	if (dwZoneIsAll())
	{
		int high = s_zone.NOT_IN_CODEPLUGDATA_highestIndex;
		if (high < 1) return 1;
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

static int16_t dwInitialIdx(void)
{
	return dwZoneIsAll() ? (int16_t)1 : (int16_t)0;
}

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

/* Which local index is the focused channel?  Returns -1 if focus is on
 * the zone row (no channel under focus). */
static int16_t dwFocusedIdx(void)
{
	if (s_focus == FOCUS_CHA) return s_chAIdx;
	if (s_focus == FOCUS_CHB) return s_chBIdx;
	return -1;
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

/* Program the AT1846 for RX on the given channel.  Uses a stack-local
 * struct so channelScreenChannelData (= the PTT data target) is left
 * alone — see programRadioForActiveSide() below for the rationale. */
static void programRadioForChannel(uint16_t chNum)
{
	if (chNum == 0) return;
	CodeplugChannel_t tmp;
	codeplugChannelGetDataForIndex(chNum, &tmp);
	trxRxAndTxOff(true);
	trxSetModeAndBandwidth(tmp.chMode,
	                       (codeplugChannelGetFlag(&tmp, CHANNEL_FLAG_BW_25K) != 0));
	trxSetFrequency(tmp.rxFreq, tmp.txFreq, DMR_MODE_AUTO);
	trxSetRxCSS(RADIO_DEVICE_PRIMARY, tmp.rxTone);
	trxRxOn(true);
}

/* Re-tune the radio for whichever side is currently keyed.  Called on
 * every swap and after returning from a TX. */
static void programRadioForActiveSide(void)
{
	int16_t idx = (s_activeAB == 0) ? s_chAIdx : s_chBIdx;
	programRadioForChannel(dwIdxToChannelNum(idx));
}

/* Put the focused channel into channelScreenChannelData so the PTT
 * handler in applicationMain.c picks up the right txFreq / chMode /
 * CSS when the user presses PTT.  Does NOT program the radio — the
 * alternation tick handles RX programming separately, leaving the
 * hardware free to follow A<->B independently of the focus. */
static void publishFocusedChannelToGlobals(void)
{
	int16_t idx = dwFocusedIdx();
	if (idx < 0)
	{
		/* Zone focus — fall back to whichever side is currently keyed
		 * so PTT does something sensible (TX on the channel the user
		 * is presently hearing). */
		idx = (s_activeAB == 0) ? s_chAIdx : s_chBIdx;
	}
	uint16_t chNum = dwIdxToChannelNum(idx);
	if (chNum == 0) return;

	codeplugChannelGetDataForIndex(chNum, &channelScreenChannelData);
	currentChannelData = &channelScreenChannelData;
	uiDataGlobal.currentSelectedChannelNumber = chNum;
}

/* ---------------------------------------------------------------------- */
/* Zone reload + clamp                                                     */
/* ---------------------------------------------------------------------- */

static void reloadZone(int16_t newZoneIdx)
{
	s_zoneIdx = newZoneIdx;
	codeplugZoneGetDataForNumber(s_zoneIdx, &s_zone);
	s_chAIdx = dwClampIdx(s_chAIdx);
	s_chBIdx = dwClampIdx(s_chBIdx);
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
		if (!s_initialized)
		{
			/* Fresh entry from the quick menu — full init. */
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
			s_initialized = true;
		}
		/* Re-entering after a PTT round-trip (UI_TX_SCREEN popped back
		 * to us with isFirstRun=true) — preserve every state variable,
		 * just retune the radio for the current side and refresh the
		 * display. */
		s_lastSwapMs = ticksGetMillis();
		programRadioForActiveSide();
		publishFocusedChannelToGlobals();
		voicePromptsInit();
		voicePromptsAppendLanguageString(currentLanguage->dual_watch);
		voicePromptsPlay();
		render();
		return MENU_STATUS_SUCCESS;
	}

	uint32_t now = ticksGetMillis();
	bool carrier = trxCarrierDetected(RADIO_DEVICE_PRIMARY);

	/* Auto-jump: when scanning B and a carrier breaks squelch on B,
	 * commit to that channel — stop the sweep and move focus to B so
	 * the operator can PTT on whatever they just heard.  Then fall
	 * through; the carrier-pause check below will hold the side at B
	 * for as long as the carrier is present. */
	if (s_scanningB && (s_activeAB == 1) && carrier)
	{
		s_scanningB = false;
		s_focus     = FOCUS_CHB;
		publishFocusedChannelToGlobals();
		render();
	}

	/* Unified swap-and-scan tick.  Every DW_SWAP_MS:
	 *   - swap which side is keyed
	 *   - if scanning and we're moving A→B, step B's index first so the
	 *     side we're about to key is the next channel in the sweep
	 *   - retune the radio for the new active side
	 * Carrier on the keyed side pauses the swap so an incoming call
	 * isn't truncated mid-syllable. */
	if (((now - s_lastSwapMs) >= DW_SWAP_MS) && !carrier)
	{
		s_lastSwapMs = now;
		bool aToB = (s_activeAB == 0);
		s_activeAB ^= 1u;
		if (aToB && s_scanningB)
		{
			s_chBIdx = dwAdvanceIdx(s_chBIdx, s_scanDir);
		}
		programRadioForActiveSide();
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
		/* Sync the channel-mode selection to whatever the focus points
		 * at (or the currently-keyed side if focus is on Zone). */
		publishFocusedChannelToGlobals();
		/* channelScreenChannelData is now correct — but the channel
		 * screen looks at its rxFreq == 0 sentinel to decide whether
		 * to reload from codeplug on next entry.  We just wrote a real
		 * channel into it, so it won't reload; that's intentional. */
		s_initialized = false;  /* next open will do a fresh init */
		menuSystemPopPreviousMenu();
		return MENU_STATUS_SUCCESS;
	}

	/* Long-press STAR: start the B scan.  Auto-focuses to B so the
	 * caller doesn't have to cycle there first, and uses the forward
	 * direction as default — the user can reverse by tapping the
	 * normal cycle controls. */
	if (KEYCHECK_LONGDOWN(ev->keys, KEY_STAR))
	{
		s_focus     = FOCUS_CHB;
		s_scanningB = true;
		s_scanDir   = 1;
		publishFocusedChannelToGlobals();
		render();
		return MENU_STATUS_SUCCESS;
	}

	/* Short STAR: cycle focus. */
	if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		s_focus = (DwFocus_t)((s_focus + 1) % FOCUS_COUNT);
		publishFocusedChannelToGlobals();
		render();
		return MENU_STATUS_SUCCESS;
	}

	/* UP / DOWN (or rotary) cycle the focused field by one step.  The
	 * old LEFT/RIGHT bindings are still accepted as an alternate
	 * because they're harmless if pressed accidentally. */
	bool up   = KEYCHECK_SHORTUP(ev->keys, KEY_UP)    ||
	            KEYCHECK_SHORTUP(ev->keys, KEY_LEFT)  ||
	            KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT);
	bool down = KEYCHECK_SHORTUP(ev->keys, KEY_DOWN)  ||
	            KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT) ||
	            KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT);

	if (up || down)
	{
		int delta = down ? 1 : -1;

		if (s_focus == FOCUS_ZONE)
		{
			int zc = codeplugZonesGetCount();
			if (zc > 0)
			{
				reloadZone((int16_t)(((int)s_zoneIdx + delta + zc) % zc));
				programRadioForActiveSide();
			}
		}
		else
		{
			int16_t *target = (s_focus == FOCUS_CHA) ? &s_chAIdx : &s_chBIdx;
			*target = dwAdvanceIdx(*target, delta);
			/* If we just changed the side that's currently keyed, retune. */
			if (((s_focus == FOCUS_CHA) && (s_activeAB == 0)) ||
			    ((s_focus == FOCUS_CHB) && (s_activeAB == 1)))
			{
				programRadioForActiveSide();
			}
		}
		publishFocusedChannelToGlobals();
		render();
	}

	return MENU_STATUS_SUCCESS;
}
