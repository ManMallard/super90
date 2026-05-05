/*
 * enc_indicator.h — small helper to render an "ENC" badge whenever the
 * current channel has an encryption key configured.
 *
 * Active when ALL of:
 *   - currentChannelData is non-NULL
 *   - chMode == RADIO_MODE_DIGITAL
 *   - encKeyIndex is in 1..16
 *   - that key slot is populated (passphrase or hex)
 */
#ifndef ENC_INDICATOR_H
#define ENC_INDICATOR_H

#include <stdbool.h>

bool enc_indicator_is_active(void);
void enc_indicator_draw(void);   /* call from the header rendering function */

#endif
