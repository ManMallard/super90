# OpenGD77 AES-256 Encryption Patch (STM32 / OpenMDUV380)

Adds AES-256-CTR voice encryption to the OpenMDUV380 firmware that runs on
the TYT MD-UV380 / MD-UV390 / MD-UV390 Plus, the Retevis RT-3S, and the
Baofeng DM-1701 / Retevis RT-84.

## What this gives you

- A 16-slot key bank, accessed from a new top-level "Encryption Keys" menu
  (shown as a duplicate "Credits" entry until a custom language string is
  added — see "Polish" below).
- Per-slot key entry by either:
  - **T9 multi-tap passphrase** (PBKDF2-HMAC-SHA256, 4000 iterations)
  - **64-character hex** (direct 32-byte key)
- Per-channel encryption-key selector in **Channel details** ("Enc Key" row,
  cycle 0..16 with LEFT/RIGHT keys).
- "ENC" badge on the main screen when an encrypted channel is active.
- AES-256-CTR encrypt/decrypt on the 27-byte AMBE+2 voice frames as they
  pass through the HR-C6000 SPI buffer.

## What this does not do

- **Bench testing only.** Encrypting voice on amateur frequencies is
  prohibited in most jurisdictions. Use only on shielded test setups
  (dummy loads, attenuators, screened rooms) or on FCC Part 90 / commercial
  spectrum where you are licensed.
- **No interop with TYT BP encryption.** This is a separate, stronger
  scheme. Two radios with this patch can talk to each other; neither will
  understand a plain TYT-encrypted transmission.
- **No automatic key exchange.** You must pre-share the passphrase or hex
  key out of band on every radio that will use a slot.

## Prerequisites

- Windows 10 or 11
- Python 3.9+ on PATH
- STM32CubeIDE
- The OpenMDUV380 source tree (extracted from the official `.zip` from
  `opengd77.com`)

## Apply

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1 `
    -Repo C:\sneaky390-uv380\OpenGD77_MDUV380_DM1701_20260130
```

The script will:

1. Run `prepare.bat` to generate the AMBE codec placeholder
   (`codec_bin_section_1.bin`).
2. Copy `new_files/source/crypto/*.c` and `new_files/include/crypto/*.h`
   into `MDUV380_firmware/application/{source,include}/crypto/`.
3. Copy `menuKeyManagement.c` and `menuKeyEntry.c` into
   `MDUV380_firmware/application/source/user_interface/`.
4. Run `apply_mods.py`, which:
   - Renames `_UNUSED_2` to `encKeyIndex` in `CodeplugChannel_t`.
   - Inserts the encrypt/decrypt taps at three call sites in `HR-C6000.c`.
   - Adds `MENU_KEY_MANAGEMENT` and `MENU_KEY_ENTRY` to the `MENU_SCREENS`
     enum, with matching prototypes.
   - Appends two rows to `menuFunctions[]` in `menuSystem.c` and one
     entry to `mainMenuItems[]`.
   - Adds the `CH_DETAILS_ENC_KEY` row to `menuChannelDetails.c`
     (enum + render + LEFT + RIGHT handlers).
   - Calls `enc_indicator_render()` from the main-screen header path.
   - Patches `codec_interface.c` and `codec.h` to use `LDR/BLX` for the
     AMBE codec calls instead of `BL <literal>`. This is required for
     STM32CubeIDE 2.1+ (GCC 14.x) — older toolchains accepted the bare
     `BL <literal>` form, but newer binutils rejects it as a "dangerous
     relocation". Without this fix, the linker fails with
     `Unknown destination type (ARM/Thumb)`.

Anything the patcher can't safely apply ends up in `AES_PATCH_TODO.md` at
the repo root.

## Build

**Before the first build, generate the AMBE codec placeholder.** The
official source ships without the proprietary AMBE codec — it's spliced
in by the CPS firmware loader at flash time. The build needs a
zero-filled placeholder file in its place or the assembler errors out
with `file not found: codec_bin_section_1.bin`.

From a Windows Command Prompt at the source root:

```cmd
prepare.bat
```

This calls `MDUV380_firmware\tools\codec_cleaner.exe -C` which writes a
~298 KB placeholder to `MDUV380_firmware\application\source\linkerdata\`.
You only need to do this once per fresh source extract. (`setup.ps1`
runs it automatically.)

Then:

1. Open STM32CubeIDE.
2. **File → Open Projects from File System →** select the
   `MDUV380_firmware` folder.
3. **Project → Clean…**, then **Project → Build Project**.
4. The output is a `.bin` (and the post-build step packages a `.sgl`) in
   the build configuration's folder.

## Flash

1. Open the OpenGD77 CPS.
2. Connect the radio in firmware-update mode.
3. **Extras → Firmware Loader** (menu name varies by CPS version).
4. Select the `.sgl` (or `.bin` plus the donor `.bin` if your CPS asks for
   one).
5. Wait for "Firmware update complete." Power-cycle the radio.

## Verify

After power-on:

1. **Menu** → scroll to a second "Credits" entry → opens 16-slot key list.
2. Press **Green** on slot 1, enter a T9 passphrase like `TEST`, then
   **Green** to commit. The slot now shows label "Slot 1".
3. **Menu → Channel details** on a digital channel. Scroll to "Enc Key".
   Press **Right** to set it to `1`. **Green** to confirm.
4. The "ENC" badge should appear in the upper-right of the main screen
   when that channel is active.
5. Repeat steps 1-3 on a second radio with the same passphrase on slot 1.
6. Key up on radio A. Radio B should decode voice. Without matching keys,
   radio B receives noise.

## Polish (optional, after the bench test works)

- Add a real "Enc Keys" language string instead of reusing the index 2
  ("Credits") slot. Requires a field in `uiLocalisation.h` and an entry
  in every `.h` file under `application/include/user_interface/languages/`.
- Tune `PBKDF2_ITERATIONS` in `kdf.h` if entry feels slow.
- Replace the `dmr_crypto_make_nonce` software mixer with the STM32 RNG
  peripheral for production-quality nonces.
- Wire up per-PTT session keying — currently nonces are persistent across
  power cycles only. For deployment, generate a fresh nonce on PTT-down,
  send it in the voice header, and reset the superframe counter.

## Layout

```
opengd77-aes-patch/
├─ README.md                 — this file
├─ setup.ps1                 — entry point
├─ apply_mods.py             — anchored patcher
└─ new_files/
   ├─ source/
   │  ├─ crypto/             — aes.c sha256.c kdf.c dmr_crypto.c key_storage.c enc_indicator.c
   │  └─ user_interface/     — menuKeyManagement.c menuKeyEntry.c
   └─ include/
      └─ crypto/             — matching headers
```
