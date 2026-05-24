/*
 * Renders the "ENC" badge in the bottom-right area of the main screens
 * when the active channel is DMR or M17 and has a non-zero encKeyIndex.
 *
 * States:
 *   1. Channel encKeyIndex == 0           -> nothing shown
 *   2. encKeyIndex > 0 and slot populated -> "ENC" badge bottom-right
 *   3. encKeyIndex > 0 but slot EMPTY     -> "ENC ACTIVE NO KEY NO ENC"
 *      warning shown ONLY during TX (PTT pressed). The TX taps still see
 *      dmr_crypto_tx_active() == 0 in this state, so audio transmits
 *      unencrypted; the warning makes it visible to the user that
 *      encryption was requested but is not happening for this PTT.
 *   4. PTT-mode slot, RX active, incoming LC has NO magic byte ->
 *      "UNENC RX" badge + alternating red/green LED flash. Tells the user
 *      they're receiving plaintext on a channel they expected to be encrypted.
 *
 * Call enc_indicator_render() from the main-screen render path after
 * the standard header has been drawn.
 */
#include "crypto/enc_indicator.h"
#include "crypto/key_storage.h"
#include "crypto/dmr_crypto.h"    /* aes_patch_engage_for_current_channel,
                                     dmr_crypto_rx_should_decrypt_this_call */
#include "functions/codeplug.h"   /* CodeplugChannel_t */
#include "functions/trx.h"        /* RADIO_MODE_DIGITAL, trxTransmissionEnabled */
#include "functions/ticks.h"      /* ticksGetMillis */
#include "hardware/HR-C6000.h"    /* slotState, DMR_STATE_RX_* */
#include "hardware/HX8353E.h"     /* displayPrintAt, FONT_SIZE_1 */
#include "io/LEDs.h"              /* LedWrite, LED_RED, LED_GREEN */
#include "user_interface/uiGlobals.h" /* DISPLAY_Y_POS_CONTACT_TX */
#include <stddef.h>

extern CodeplugChannel_t *currentChannelData;
extern volatile bool trxTransmissionEnabled;

/* AES patch: track LED state so we restore it when the unenc-rx warning ends.
 * The radio normally uses LED_GREEN for RX activity; we override it during
 * the warning flash and need to put it back. */
static uint8_t s_unencFlashActive = 0;

static int rx_is_active(void)
{
    return (slotState == DMR_STATE_RX_1) || (slotState == DMR_STATE_RX_2);
}

static int unenc_rx_warning_should_show(void)
{
    if (currentChannelData == NULL) return 0;
    if (currentChannelData->chMode != RADIO_MODE_DIGITAL) return 0;
    uint8_t idx = currentChannelData->encKeyIndex;
    if (idx == 0 || idx > KEY_SLOT_COUNT) return 0;
    if (!keystore_is_set(idx)) return 0;        /* covered by NO KEY warning */
    if (!rx_is_active()) return 0;              /* no incoming call right now */
    /* Autodetect gate is 0 only when the LC parser saw a plaintext call
     * on a PTT-mode slot. That's exactly the case we want to flag. */
    if (dmr_crypto_rx_should_decrypt_this_call()) return 0;
    return 1;
}

void enc_indicator_render(void)
{
    /* AES patch: keep crypto state synced with currentChannelData on every
     * header redraw. Idempotent — re-init only fires when channel/key state
     * changes. This makes RX hot from the moment a channel is loaded
     * (no need to PTT first). */
    aes_patch_engage_for_current_channel();

    if (currentChannelData == NULL) goto end_no_warn;
    /* Show ENC badge for both DMR and M17 — both use the same key slot. */
    if (currentChannelData->chMode != RADIO_MODE_DIGITAL &&
        currentChannelData->chMode != RADIO_MODE_M17) goto end_no_warn;

    uint8_t idx = currentChannelData->encKeyIndex;
    if (idx == 0 || idx > KEY_SLOT_COUNT) goto end_no_warn;

    if (!keystore_is_set(idx))
    {
        /* Assigned slot is empty — audio transmits IN THE CLEAR.
         * Show warning ONLY while PTT is pressed (trxTransmissionEnabled).
         * Position: one font row (8 px) below the talk-group / contact line
         * so it reads: TOT timer → TG number → ENC warning → channel name. */
        if (trxTransmissionEnabled) {
            displayPrintAt(0, DISPLAY_Y_POS_CONTACT_TX + 8, "ENC ACTIVE NO KEY NO ENC", FONT_SIZE_1);
        }
        goto end_no_warn;
    }

    /* UNENC RX warning uses DMR slot-state and LC magic byte — DMR only. */
    if (currentChannelData->chMode == RADIO_MODE_DIGITAL)
    {
        if (unenc_rx_warning_should_show())
        {
            displayPrintAt(0, 10, "UNENC RX", FONT_SIZE_1);
            uint32_t t = ticksGetMillis();
            if ((t / 250) & 1) {
                LedWrite(LED_RED, 1);
                LedWrite(LED_GREEN, 0);
            } else {
                LedWrite(LED_RED, 0);
                LedWrite(LED_GREEN, 1);
            }
            s_unencFlashActive = 1;
            /* Continue to also draw the standard ENC badge. */
        }
        else if (s_unencFlashActive)
        {
            /* Warning just ended — clear the LEDs we forced on. The radio's
             * normal RX/TX LED logic will re-assert correct state on the next
             * tick. */
            LedWrite(LED_RED, 0);
            LedWrite(LED_GREEN, 0);
            s_unencFlashActive = 0;
        }
    }

    /* FONT_SIZE_1 = font_6x8: 3 chars * 6 px wide = 18 px; 8 px tall.
     * x = DISPLAY_SIZE_X - 18 - 2 = right edge with 2 px margin.
     * y = DISPLAY_SIZE_Y - 8 - 1 = bottom edge with 1 px margin. */
    displayPrintAt(DISPLAY_SIZE_X - 20, DISPLAY_SIZE_Y - 9, "ENC", FONT_SIZE_1_BOLD);
    return;

end_no_warn:
    if (s_unencFlashActive) {
        LedWrite(LED_RED, 0);
        LedWrite(LED_GREEN, 0);
        s_unencFlashActive = 0;
    }
}
