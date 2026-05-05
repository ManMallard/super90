#include "enc_indicator.h"
#include "key_storage.h"
#include "functions/codeplug.h"   /* full struct definition for currentChannelData */
#include <stddef.h>               /* NULL */

extern struct_codeplugChannel_t *currentChannelData;

/* Field accessors — kept as macros for fork portability */
#define CH_MODE(c)        ((c)->chMode)
#define CH_ENC_INDEX(c)   ((c)->encKeyIndex)

/* OpenGD77 enum value for digital mode. */
#ifndef RADIO_MODE_DIGITAL
#define RADIO_MODE_DIGITAL 1
#endif

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
    /* Position: top-right corner of the 128x64 display, FONT_SIZE_1 (~6 px tall).
     * "ENC" is 3 chars * 6 px = 18 px wide; place at x=108 to leave a 2 px margin. */
    ucPrintAt(108, 0, "ENC", FONT_SIZE_1);
}
