# AES-256 patch — manual fixup required

## menuChannelDetails.c — add Encryption Key selector
Add a row in the channel-details menu enum/array that lets the user
pick `currentChannelData->encKeyIndex` in 0..16. The field is already
in the codeplug struct via the codeplug.h patch.
On render, look up `keystore_get(idx)` to show the slot label.

## menuMainMenu.c not found — register MENU_KEY_MANAGEMENT manually.

## Build system
- In MCUXpresso: right-click the project, **Refresh**, then
  **Project > Clean** to rebuild.
- The `firmware/source/crypto/` folder needs to be in the project's
  "Paths and Symbols > Source Location". MCUXpresso usually picks
  this up automatically; verify under Project Properties.
- Include path: add `firmware/source` so `#include "crypto/aes.h"` resolves.

## Makefile detected at C:\Users\PSE-Detailing-1\githubdesktop\sneaky390\OpenGD77\firmware\Makefile — add crypto/*.c to SRCS list.
