#include "enc_indicator.h"
#include "key_storage.h"

/*
 * Externs into OpenGD77 core. If your tree exposes these under different
 * names (e.g. uiDataGlobal.currentChannel), adjust accordingly.
 */
struct struct_codeplugChannel_t;
extern struct struct_codeplugChannel_t *currentChannelData;

/* Field accessors — declared as macros so they survive struct-name changes
 * across forks. The encKeyIndex field is added by the codeplug.h patch. */
#define CH_MODE(c)        ((c)->chMode)
#define CH_ENC_INDEX(c)   ((c)->encKeyIndex)

/* OpenGD77 enum value for digital mode. */
#ifndef RADIO_MODE_DIGITAL
#define RADIO_MODE_DIGITAL 1
#endif

/*
 * Display primitives. open-ham/OpenGD77 exposes ucPrintAt / FONT_SIZE_1
 * via fonts.h / display.h pulled in transitively from menuSystem.h. We
 * declare the prototype here weakly to avoid a hard include dependency
 * — the linker resolves to the real function at link time.
 */
extern void ucPrintAt(int x, int y, const char *text, int font);

#ifndef FONT_SIZE_1
#define FONT_SIZE_1 0
#endif
#ifndef FONT_SIZE_1_BOLD
#define FONT_SIZE_1_BOLD 1
#endif

bool enc_indicator_is_active(void)
{
    if (currentChannelData == NULL) return false;
    if ((int)CH_MODE(currentChannelData) != RADIO_MODE_DIGITAL) return false;
    uint8_t idx = CH_ENC_INDEX(currentChannelData);
    if (idx == 0) return false;
    return keystore_slot_in_use(idx);
}

void enc_indicator_draw(void)
{
    if (!enc_indicator_is_active()) return;
    /* FONT_SIZE_1 = font_6x8: "ENC" is 18 px wide, 8 px tall.
     * Positioned at bottom-right with 2 px right margin and 1 px bottom margin.
     * Values below are for the MDUV380/MD380 160x120 display (DISPLAY_SIZE_X=160,
     * DISPLAY_SIZE_Y=120). Adjust if porting to a platform with a different screen. */
#ifndef DISPLAY_SIZE_X
#define DISPLAY_SIZE_X 160
#endif
#ifndef DISPLAY_SIZE_Y
#define DISPLAY_SIZE_Y 120
#endif
    ucPrintAt(DISPLAY_SIZE_X - 20, DISPLAY_SIZE_Y - 9, "ENC", FONT_SIZE_1_BOLD);
}
