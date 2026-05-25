# OpenGD77 AES-256 Encryption Patch (STM32 / OpenMDUV380)

**Developer:** Johnny Bravo

Adds AES-256-CTR voice encryption to the OpenMDUV380 firmware for the TYT MD-UV380 / MD-UV390 / MD-UV390 Plus, Retevis RT-3S, and Baofeng DM-1701 / Retevis RT-84.

This patcher is used to apply encryption, M17 mode, and other enhancements to the official OpenGD77 MDUV380 firmware distribution. It integrates cleanly with the existing OpenGD77 source without modifying unrelated functionality.

## What This Adds

### Encryption Features
- **16-slot AES-256 key bank** with independent key management
- **Two key entry methods:**
  - T9 multi-tap passphrase (PBKDF2-HMAC-SHA256, 4000 iterations) — simple to remember
  - Direct 64-character hexadecimal (256-bit) key entry
- **Per-channel encryption control** — select which key (0–16) to use in Channel details
- **Visual indicator:** "ENC" badge displays in bottom-right of main screen when encrypted channel is active
- **Real-time crypto:** AES-256-CTR encrypt/decrypt runs on 27-byte AMBE+2 voice frames in the HR-C6000 SPI stream

### Additional Features (Included in Full Build)
- **M17 Digital Mode** — Open-source alternative to DMR, integrated into mode selector
- **GPS Waypoints** — Track and save GPS coordinates with distance calculations
- **UI & Memory Optimizations** — Bug fixes and flash/RAM optimizations

## ⚠️ Critical Limitations & Legal Notice

### Bench Testing Only
**Encrypting voice on amateur radio frequencies is illegal in most jurisdictions** (USA, Canada, EU, and others). This implementation is for testing on:
- Shielded test setups (dummy loads, attenuators, screened Faraday cages)
- Commercial licensed spectrum where encryption is permitted (FCC Part 90, etc.)

**Unauthorized use of encryption on amateur frequencies can result in:**
- Heavy FCC fines ($10,000+)
- Criminal prosecution
- License revocation
- Other civil/criminal penalties

Users assume **all responsibility** for compliance with local regulations. Neither the developer nor OpenGD77 project provides any legal warranty or liability protection.

### Encryption Limitations
- **Not compatible with TYT BP encryption** — entirely separate cipher with different keys and implementation
- **Two radios with this patch** can communicate with matching passphrases; neither will decode TYT-encrypted transmissions
- **No automatic key exchange** — you must pre-share the passphrase or hex key out-of-band on every radio using a slot
- **Nonce generation is software-based** and persistent across power cycles (suitable for testing; production deployments would use RNG peripheral for per-PTT nonces)

## What Stays the Same

This patcher **does not modify** core OpenGD77 functionality:
- DMR mode and architecture remain unchanged
- FM analog mode is unaffected
- Hotspot mode works as in base OpenGD77
- Codeplug system and zone/contact management are compatible
- All OpenGD77 standard menus and UI paradigms are preserved
- Flashing procedure is identical to standard OpenGD77

The enhancements are additive — existing OpenGD77 users can apply this patch to gain new capabilities without losing any base functionality.

## Prerequisites

- **Windows 10 or 11**
- **Python 3.9+** (must be on system PATH)
- **STM32CubeIDE 2.1+** — [download from st.com](https://www.st.com/en/development-tools/stm32cubeide.html)
- **OpenGD77 MDUV380 source tree** — Extract the official `.zip` from [opengd77.com](https://opengd77.com) (do NOT git clone; use the official release `.zip`)

## Apply the Patcher

From the root of the sneaky390 repository:

```powershell
powershell -ExecutionPolicy Bypass -File .\Patcher\setup.ps1 `
    -Repo C:\path\to\OpenGD77_MDUV380_DM1701_YYYYMMDD
```

Replace the path with your extracted OpenGD77 official source directory. Do not use a git clone; the patcher expects the official `.zip` distribution.

### What the Patcher Does

1. **Generates AMBE placeholder** — Runs `prepare.bat` to create the codec stub (`codec_bin_section_1.bin`)
2. **Copies encryption source** — Installs `crypto/*.c` and `crypto/*.h` files into the application directory
3. **Adds UI files** — Copies key management menus (`menuKeyManagement.c`, `menuKeyEntry.c`)
4. **Integrates changes** — Runs `apply_mods.py` to:
   - Redefine unused codeplug fields for encryption key storage
   - Insert encrypt/decrypt hooks into the HR-C6000 SPI stream (3 call sites)
   - Register new menu screens and UI handlers
   - Add the "Enc Key" row to channel details
   - Display "ENC" indicator on the main screen
   - **Fix GCC 14.x compatibility** — Converts `BL <literal>` AMBE codec calls to `LDR/BLX` form (required for STM32CubeIDE 2.1+)
   - Add call to `enc_indicator_render()` to the main screen render loop

### Incomplete Patches

If `apply_mods.py` encounters sections it cannot safely modify, it outputs `AES_PATCH_TODO.md` at the repo root with manual instructions. This is rare but may occur if the upstream OpenGD77 source differs significantly from expected.

## Build Instructions

See the [main README](../README.md) for complete build instructions, including prerequisites and troubleshooting.

### Quick Summary

1. Run `Patcher\setup.ps1` (see above) — this generates the AMBE placeholder and applies all patches
2. Open STM32CubeIDE and load `MDUV380_firmware` folder
3. **Project → Clean**, then **Project → Build Project**
4. Output files (`.sgl`, `.bin`, `.elf`) appear in the build folder

## ⚠️ Firmware Size Warning

This patched firmware is **significantly larger than base OpenGD77** due to:
- AES-256 cryptography library (~30 KB)
- M17 digital mode and Codec2 (~150 KB)
- Enhanced UI and menu system (~20 KB)
- Additional features and optimizations

**Verify your radio's flash layout** before flashing:
- TYT MD-UV390: ~1 MB total flash; verify remaining space after firmware load
- Older or budget variants may have less flash available

If your radio runs out of storage after loading this firmware, user codeplug data may not fit. **Back up your existing firmware before flashing.**

## Flash to Radio

1. Connect radio to PC via USB in **firmware-update mode**
2. Open **OpenGD77 CPS** (Configuration Programming Software)
3. **Extras → Firmware Loader**
4. Select the `.sgl` output from the build
5. (Some CPS versions require a donor `.bin` file as well)
6. Click **Write** and wait for completion
7. **Power cycle the radio**

## Verify Encryption Works

After flashing and power-on:

1. **Menu → Credits** → Second "Credits" entry opens the 16-slot key list
2. **Green** on slot 1 → Enter T9 passphrase (e.g., `TEST`) → **Green** to save
   - Slot now displays "Slot 1" with some visible characters from your passphrase
3. **Menu → Channel details** on a **digital channel** → Scroll to "Enc Key"
4. Press **Right** to cycle to slot `1` → **Green** to confirm
5. Main screen should show **"ENC"** badge in the bottom-right when that channel is active
6. **On a second radio:** Repeat steps 1–4 with the same passphrase on slot 1
7. **Test transmission:**
   - Radio A keys up (PTT) on the encrypted channel
   - Radio B should decode recognizable voice (if keys match)
   - Without matching keys, Radio B receives noise/garbled audio

**Test setups only** — verify both radios are isolated from RF spectrum (screened room, dummy load, attenuators) before attempting any transmission.

## Future Enhancements (Not Yet Implemented)

- **Per-PTT session keying** — Currently nonces persist across power cycles. Production would generate fresh nonces on each PTT event, send in voice header, and reset superframe counter
- **M17 encryption** — M17 mode currently does not support encryption (unlike DMR)
- **Localization** — "Enc Keys" currently appears as a duplicate "Credits" menu entry. Would require adding language strings to `uiLocalisation.h` and all language files
- **RNG peripheral** — Replace software-based nonce generator with STM32 RNG peripheral for higher quality randomness
- **PBKDF2 tuning** — Adjust `PBKDF2_ITERATIONS` in `kdf.h` if passphrase entry feels too slow or too fast

## Repository Structure

```
sneaky390/
├─ README.md                              — Main documentation (building, features, legality)
├─ Patcher/
│  ├─ README.md                           — This file
│  ├─ setup.ps1                           — Entry point (applies patcher to OpenGD77 source)
│  ├─ apply_mods.py                       — Automated patcher (integrates enhancements)
│  └─ new_files/
│     ├─ source/
│     │  ├─ crypto/
│     │  │  ├─ aes.c
│     │  │  ├─ sha256.c
│     │  │  ├─ kdf.c                      — PBKDF2 key derivation
│     │  │  ├─ dmr_crypto.c               — Encryption/decryption logic
│     │  │  ├─ key_storage.c
│     │  │  └─ enc_indicator.c            — "ENC" badge rendering
│     │  └─ user_interface/
│     │     ├─ menuKeyManagement.c        — Key slot management UI
│     │     └─ menuKeyEntry.c             — Passphrase/hex key entry UI
│     └─ include/
│        └─ crypto/                       — Matching header files (.h)
├─ MDUV380_firmware/                      — OpenGD77 firmware tree (populated by patcher)
├─ license.txt                            — BSD-3-Clause license
└─ prepare.bat                            — AMBE placeholder generator

```

## Credits

**Johnny Bravo** — AES-256 encryption implementation, M17 digital mode integration, GPS waypoints, UI enhancements, firmware optimizations, and this patcher

**OpenGD77 Team** — Base firmware, DMR architecture, FM support, and original UI framework

## License

This patcher and all source modifications are distributed under the BSD-3-Clause license (see `license.txt`). OpenGD77 and its dependencies retain their original licenses. See the OpenGD77 repository for upstream licensing details.
