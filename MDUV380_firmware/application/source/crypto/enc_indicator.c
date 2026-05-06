/*
 * Renders the "ENC" badge in the top-right area of the main screens
 * when the active channel is digital and has a non-zero encKeyIndex.
 *
 * Three states:
 *   1. Channel encKeyIndex == 0           -> nothing shown
 *   2. encKeyIndex > 0 and slot populated -> "ENC" badge upper-right
 *   3. encKeyIndex > 0 but slot EMPTY     -> "ENC ACTIVE NO KEY NO ENC"
 *      warning shown ONLY during TX (PTT pressed). The TX taps still see
 *      dmr_crypto_tx_active() == 0 in this state, so audio transmits
 *      unencrypted; the warning makes it visible to the user that
 *      encryption was requested but is not happening for this PTT.
 *
 * Call enc_indicator_render() from the main-screen render path after
 * the standard header has been drawn.
 */
#include "crypto/enc_indicator.h"
#include "crypto/key_storage.h"
#include "crypto/dmr_crypto.h"    /* aes_patch_engage_for_current_channel */
#include "functions/codeplug.h"   /* CodeplugChannel_t */
#include "functions/trx.h"        /* RADIO_MODE_DIGITAL, trxTransmissionEnabled */
#include "hardware/HX8353E.h"     /* displayPrintAt, FONT_SIZE_1 */
#include <stddef.h>

extern CodeplugChannel_t *currentChannelData;
extern volatile bool trxTransmissionEnabled;

void enc_indicator_render(void)
{
    /* AES patch: keep crypto state synced with currentChannelData on every
     * header redraw. Idempotent — re-init only fires when channel/key state
     * changes. This makes RX hot from the moment a channel is loaded
     * (no need to PTT first). */
    aes_patch_engage_for_current_channel();

    if (currentChannelData == NULL) return;
    if (currentChannelData->chMode != RADIO_MODE_DIGITAL) return;

    uint8_t idx = currentChannelData->encKeyIndex;
    if (idx == 0 || idx > KEY_SLOT_COUNT) return;

    if (!keystore_is_set(idx))
    {
        /* AES patch: assigned slot is empty - audio transmits IN THE CLEAR.
         * Show warning ONLY while PTT is pressed (trxTransmissionEnabled),
         * and on the SECOND line so we don't collide with the normal header. */
        if (trxTransmissionEnabled) {
            displayPrintAt(0, 10, "ENC ACTIVE NO KEY NO ENC", FONT_SIZE_1);
        }
        return;
    }

    /* 16x6 px badge in upper-right area. Y=1 (was 0) for vertical breathing room. */
    displayPrintAt(108, 1, "ENC", FONT_SIZE_1);
}
