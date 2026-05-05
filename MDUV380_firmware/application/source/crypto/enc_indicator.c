/*
 * Renders the "ENC" badge in the top-right area of the main screens
 * when the active channel is digital and has a non-zero encKeyIndex.
 *
 * Call enc_indicator_render() from the main-screen render path after
 * the standard header has been drawn.
 */
#include "crypto/enc_indicator.h"
#include "crypto/key_storage.h"
#include "functions/codeplug.h"   /* CodeplugChannel_t */
#include "functions/trx.h"        /* RADIO_MODE_DIGITAL */
#include "hardware/HX8353E.h"     /* displayPrintAt, FONT_SIZE_1 */
#include <stddef.h>

extern CodeplugChannel_t *currentChannelData;

void enc_indicator_render(void)
{
    if (currentChannelData == NULL) return;
    if (currentChannelData->chMode != RADIO_MODE_DIGITAL) return;

    uint8_t idx = currentChannelData->encKeyIndex;
    if (idx == 0 || idx > KEY_SLOT_COUNT) return;
    if (!keystore_is_set(idx)) return;

    /* 16x6 px badge in upper-right area. Adjust X if it collides on your
     * display variant. */
    displayPrintAt(108, 0, "ENC", FONT_SIZE_1);
}
