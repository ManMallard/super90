# Super90 / sneaky390 Patcher (STM32 / OpenMDUV380)

Applies the Super90 / sneaky390 patch set to a fresh, official OpenGD77 MDUV380 source tree. Supported radios: TYT MD-UV380 / MD-UV390 / MD-UV390 Plus, Retevis RT-3S, and Baofeng DM-1701 / Retevis RT-84.

The patcher uses **3-way merge** so it will continue to work against future OpenGD77 releases — upstream's fixes are preserved automatically alongside our patches wherever they don't conflict.

## Contents

- [What This Adds](#what-this-adds)
- [Critical Limitations & Legal Notice](#️-critical-limitations--legal-notice)
- [What Stays the Same](#what-stays-the-same)
- [Prerequisites](#prerequisites)
- [**Implementation Steps**](#implementation-steps)
  - Step 1 — Acquire a fresh OpenGD77 release
  - Step 2 — Clone or download this repo
  - Step 3 — Run the patcher
  - Step 4 — Read `PATCH_LOG.md`
  - Step 5 — Resolve `CONFLICT` / `MISSING` / `MANUAL` items
  - Step 6 — Open the project in STM32CubeIDE
  - Step 7 — Build
  - Step 8 — Flash the radio
- [What the Patcher Does (technical detail)](#what-the-patcher-does-technical-detail)
- [Why 3-way merge?](#why-3-way-merge-not-anchored-edits-not-full-file-replacement)
- [Firmware Size Warning](#️-firmware-size-warning)
- [Flash to Radio](#flash-to-radio)
- [Verify Encryption Works](#verify-encryption-works)
- [Future Enhancements](#future-enhancements-not-yet-implemented)
- [Repository Structure](#repository-structure)
- [License](#license)

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

- **Windows 10 or 11** (Linux/macOS works too — use `setup.sh` in place of `setup.ps1`)
- **Python 3.9+** on the system `PATH`
- **`git` on the system `PATH`** — used by the patcher for 3-way merge. Git for Windows (bundled with GitHub Desktop) satisfies this. Verify with `git --version` from PowerShell.
- **STM32CubeIDE 2.1+** — [download from st.com](https://www.st.com/en/development-tools/stm32cubeide.html)
- **OpenGD77 MDUV380 source tree** — Extract the official `.zip` from [opengd77.com](https://opengd77.com). Do NOT `git clone`; the patcher expects the official `.zip` distribution.

## Implementation Steps

You only need to run the patcher if you want to apply our changes to a **newer** OpenGD77 release than the one we built against. If you just want to build/flash the firmware as-is, skip to **Step 6** — the `MDUV380_firmware/` folder in this repo is already patched.

### Step 1 — Acquire a fresh OpenGD77 release

1. Go to [opengd77.com](https://opengd77.com) → Downloads.
2. Download the latest `OpenGD77_MDUV380_DM1701_YYYYMMDD.zip` (or whichever date you want to build against).
3. Extract to a path with no spaces, e.g. `C:\OpenGD77_MDUV380_DM1701_YYYYMMDD\`.
4. Verify the layout — you should see:
   ```
   C:\OpenGD77_MDUV380_DM1701_YYYYMMDD\
     ├─ MDUV380_firmware\
     │   ├─ application\
     │   │   ├─ include\
     │   │   └─ source\
     │   └─ tools\
     ├─ prepare.bat
     └─ license.txt
   ```

### Step 2 — Clone or download this repo

If you don't already have it:

```powershell
git clone https://github.com/ManMallard/sneaky390.git C:\sneaky390
```

### Step 3 — Run the patcher

From any directory:

```powershell
powershell -ExecutionPolicy Bypass -File C:\sneaky390\Patcher\setup.ps1 `
    -Repo C:\OpenGD77_MDUV380_DM1701_YYYYMMDD
```

On Linux/macOS:

```bash
bash /path/to/sneaky390/Patcher/setup.sh /path/to/OpenGD77_MDUV380_DM1701_YYYYMMDD
```

What this does, in order:

1. **Verifies the target tree** — confirms `MDUV380_firmware\application\` exists.
2. **Generates the AMBE codec placeholder** — runs `prepare.bat` to produce `codec_bin_section_1.bin` (the CPS splices the real codec in at flash time).
3. **Verifies `git` is on `PATH`** — needed for 3-way merge. Aborts with a clear error if missing.
4. **Installs new files (Phase 1)** — copies AES crypto, M17 stack, Codec2, key-management UI, etc. from `new_files/` into the target tree.
5. **Applies modifications via 3-way merge (Phase 2)** — for every upstream file we patch, runs `git merge-file OURS BASE THEIRS`. Upstream's changes since we forked are preserved automatically alongside our patches wherever they don't overlap. Overlaps are flagged with conflict markers.
6. **Writes `PATCH_LOG.md`** at the root of your target tree — a full log of every action.

### Step 4 — Read `PATCH_LOG.md`

The patcher writes `PATCH_LOG.md` at the root of your target tree (e.g. `C:\OpenGD77_MDUV380_DM1701_YYYYMMDD\PATCH_LOG.md`). On Windows the setup script will offer to open it automatically when anything needs manual review.

Possible statuses in the log:

| Status | Meaning | Action |
|--------|---------|--------|
| `NEW` | New file installed (no upstream version exists) | None — informational |
| `OK` | Target was identical to OpenGD77 reference; our patches applied cleanly | None |
| `OK_MERGED` | Upstream changed this file since we forked; merge succeeded cleanly (no overlap) | Optionally review the upstream diff shown in the log — purely informational |
| `SKIP` | Target already at our patched version (idempotent re-run) | None |
| `CONFLICT` | Upstream and our patches both touched the same lines | **Required** — see Step 5 |
| `MISSING` | File expected in target tree not found (upstream renamed/moved it) | **Required** — see Step 5 |
| `MANUAL` | Patcher could not safely act (e.g. existing conflict markers) | **Required** — see Step 5 |

Exit code from `setup.ps1`: `0` = all clean, `3` = items need manual review.

### Step 5 — Resolve any `CONFLICT` / `MISSING` / `MANUAL` items

#### Resolving a `CONFLICT`

The affected file now contains one or more conflict regions like this:

```
<<<<<<< OURS (sneaky390 patch for MDUV380_firmware/application/source/hardware/HR-C6000.c)
	/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (local). */
	if (dmr_crypto_tx_active() && ...) {
		dmr_crypto_tx_frame(...);
	}
	SPI1WritePageRegByteArray(0x03, 0x00, ..., AMBE_AUDIO_LENGTH);
=======
	SPI1WritePageRegByteArray(0x03, 0x00, ..., AMBE_NEW_LENGTH);
>>>>>>> THEIRS (your fresh-tree MDUV380_firmware/application/source/hardware/HR-C6000.c)
```

1. Open the file in an editor (VS Code, Notepad++, etc.). Modern editors will syntax-highlight the conflict markers.
2. Decide for each conflict region:
   - **Take ours** — delete the `=======` and `>>>>>>>` markers and the `THEIRS` block.
   - **Take theirs** — delete the `<<<<<<<` and `=======` markers and the `OURS` block.
   - **Merge by hand** — combine the two sides (typical case: re-apply our patch on top of upstream's new code), then delete all three markers.
3. Save the file.
4. Re-run `setup.ps1` — it will `SKIP` the now-resolved file.

The same conflict block is also embedded in `PATCH_LOG.md` for easy reference.

#### Resolving `MISSING`

`MISSING` means upstream moved or renamed a file we patch. Open `Patcher/modified_files/<rel-path>` and `Patcher/upstream_reference/<rel-path>` to see what the file used to contain and what we changed in it, then apply the equivalent changes to wherever upstream put it in the new release.

#### Resolving `MANUAL`

Most common cause: leftover `<<<<<<<` markers from a previous run that the user hasn't resolved yet. Resolve them and re-run the patcher.

### Step 6 — Open the project in STM32CubeIDE

1. Launch **STM32CubeIDE**.
2. **File → Open Projects from File System…**
3. Select the `MDUV380_firmware\` folder inside your patched target tree.
4. Click **Finish**.

### Step 7 — Build

1. **Project → Clean…** (select the MDUV380_firmware project, click Clean).
2. **Project → Build Project**. First build takes a few minutes.
3. On success, find the outputs in `MDUV380_firmware\<config>\MDUV380_firmware.sgl` (and `.bin`, `.elf`, `.map`).

If you get compile errors:

- **`codec_bin_section_1.bin: file not found`** — Step 3 didn't generate the placeholder. Re-run `prepare.bat` manually inside the target tree root.
- **BLX / BL relocation errors in `codec_interface.c`** — your GCC version is older than 14; the patch already inserts the `LDR R12, =; BLX R12` form. If you're seeing this anyway, you may have applied the patch to an already-patched tree without the `SKIP` path firing — re-run the patcher and check `PATCH_LOG.md`.
- **`stringsTable_t` size mismatch / language struct errors** — upstream bumped `LANGUAGE_TAG_VERSION` or added fields. Check `PATCH_LOG.md` for `CONFLICT` entries on `uiLanguage.h` and the language headers; resolve them.

### Step 8 — Flash the radio

See [Flash to Radio](#flash-to-radio) below.

### What the Patcher Does (technical detail)

1. **Generates AMBE placeholder** — Runs `prepare.bat` to create the codec stub (`codec_bin_section_1.bin`)
2. **Installs new files** — Copies wholly-new sources from `new_files/` into the target tree (AES crypto, M17 stack, Codec2, key-management UI, etc.)
3. **Applies modifications via 3-way merge** — For each upstream file we modify (HR-C6000, codeplug, menus, language strings, etc.), `apply_mods.py` runs a standard 3-way merge (`git merge-file`):

   - **BASE** = the OpenGD77 file we forked from (`Patcher/upstream_reference/`)
   - **OURS** = our patched version (`Patcher/modified_files/`)
   - **THEIRS** = the user's fresh-tree file (whatever OpenGD77 release they're applying against)

   The result lands as: `OURS + (THEIRS − BASE)` — both upstream's changes since we forked AND our patches, layered on top of each other. Outcomes:

   - **Clean apply** (`OK`) — target is identical to BASE; our patches just apply
   - **Clean merge** (`OK_MERGED`) — upstream changed this file too, but in different lines from ours; both sets of changes are preserved automatically. The log shows a unified diff of what upstream added so you can review.
   - **Already patched** (`SKIP`) — target already matches our version; no-op
   - **Conflict** (`CONFLICT`) — upstream and we both touched the same lines; the file is written with `<<<<<<< OURS / ======= / >>>>>>> THEIRS` markers and the log lists each conflict region. Resolve in your editor, then re-run.
   - **Missing** (`MISSING`) — upstream moved/renamed the file
   - **Existing markers** (`MANUAL`) — patcher refuses to clobber unresolved conflict markers from a previous run

4. **Writes `PATCH_LOG.md`** — every action is logged. Patcher exit code `3` signals manual review needed.

### Why 3-way merge (not anchored edits, not full-file replacement)?

- **Anchored edits** are fragile against upstream changes — a reworded comment or renamed identifier silently breaks anchors. The previous patcher used these.
- **Full-file replacement** throws away every upstream improvement. If upstream fixes a bug in HR-C6000.c after we forked, naive full-file overwrite drops the fix on the floor.
- **3-way merge** is the strict superset: upstream's changes are preserved automatically wherever they don't overlap with ours, and the rare overlap is flagged loudly with a conflict block the user can resolve.

### Handling CONFLICT in `PATCH_LOG.md`

If the patcher reports `CONFLICT` for a file:

1. Open the affected file in your editor — it now contains one or more blocks like:

   ```
   <<<<<<< OURS (sneaky390 patch for ...)
   <our lines>
   =======
   <upstream's new version of the same lines>
   >>>>>>> THEIRS (your fresh-tree ...)
   ```

2. Decide which side wins, or merge by hand. Delete the `<<<<<<<` / `=======` / `>>>>>>>` markers.
3. Re-run the patcher. It will detect the resolved file and either `SKIP` or apply remaining work.

If you re-run with markers still in the file, the patcher refuses to touch it (`MANUAL` status) to prevent garbling.

### Requirements

Patcher requires `git` on `PATH` (used for `git merge-file`). Git for Windows (which ships with GitHub Desktop) satisfies this.

## Build Instructions

The full build walkthrough is in **[Implementation Steps](#implementation-steps)** above (Steps 6–7). For deeper troubleshooting, see the [main README](../README.md).

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

1. **Menu → Enc Key** → Opens the 16-slot encryption key management menu
2. **Green** on an empty slot → Enter T9 passphrase (e.g., `TEST`) → **Green** to save
   - Slot now displays with your saved key
3. **Menu → Channel details** on a **digital channel** → Scroll to "Enc Key"
4. Press **Right** to cycle to slot `1` → **Green** to confirm
5. Main screen should show **"ENC"** badge in the bottom-right corner when that encrypted channel is active
6. **On a second radio:** Repeat steps 1–4 with the same passphrase in slot 1
7. **Test transmission:**
   - Radio A keys up (PTT) on the encrypted channel
   - Radio B should decode recognizable voice (if keys match)
   - Without matching keys, Radio B receives noise/garbled audio

**Test setups only** — verify both radios are isolated from RF spectrum (screened room, dummy load, attenuators) before attempting any transmission.

## Future Enhancements (Not Yet Implemented)

- **Per-PTT session keying** — DMR mode uses an LC-steal random nonce per PTT in Option A mode; expanding this to all modes would be a refinement. Currently the DMR superframe counter is internal and resets on each `dmr_crypto_tx_init()`.
- **STM32 RNG peripheral** — Replace the software-based nonce generator with the STM32 hardware RNG for higher-quality randomness.
- **PBKDF2 tuning** — Adjust `PBKDF2_ITERATIONS` in `kdf.h` if passphrase entry feels too slow or too fast on your hardware.

## Repository Structure

```
super90/
├─ README.md                              — Main documentation (building, features, legality)
├─ Patcher/
│  ├─ README.md                           — This file
│  ├─ setup.ps1 / setup.sh                — Entry points (Windows / *nix)
│  ├─ apply_mods.py                       — Patcher: file-replacement + SHA-256 drift detection
│  ├─ new_files/                          — Wholly-new sources (no upstream version)
│  │  ├─ source/crypto/                   — AES, SHA-256, KDF, DMR crypto, key storage, ENC indicator
│  │  ├─ source/dmr_codec/codec2.c        — Codec2 for M17 voice
│  │  ├─ source/functions/m17{,_modem}.c  — M17 mode stack
│  │  ├─ source/user_interface/           — menuKeyManagement.c, menuKeyEntry.c
│  │  └─ include/                         — Matching headers for all the above
│  ├─ modified_files/                     — Our version of every upstream file we modify (~43 files)
│  └─ upstream_reference/                 — Pristine copies of those same files from the OpenGD77
│                                           release we built against. Patcher hashes the user's
│                                           fresh-tree file against this to detect upstream drift.
├─ MDUV380_firmware/                      — OpenGD77 firmware tree (pre-patched with Super90 enhancements)
├─ license.txt                            — BSD-3-Clause license
└─ prepare.bat                            — AMBE placeholder generator

```

## License

This patcher and all source modifications are distributed under the BSD-3-Clause license (see `license.txt`). OpenGD77 and its dependencies retain their original licenses. See the OpenGD77 repository for upstream licensing details.

The included MDUV380_firmware directory contains firmware enhancements based on OpenGD77 MDUV380 version 20260131, distributed under the same BSD-3-Clause terms.
