# Sneaky390 OpenGD77 Enhanced Firmware

Enhanced DMR/M17 firmware for STM32F405VGT-based transceivers (TYT MD-UV380/390, Retevis RT-3S, Baofeng DM-1701/RT-84) with AES-256 encryption, M17 digital mode support, GPS waypoints, and additional UI improvements.

**Developer:** Johnny Bravo

## Overview

This is an enhanced distribution of OpenGD77 firmware with significant additions and improvements over the base implementation. It maintains compatibility with the OpenGD77 ecosystem while adding professional-grade encryption capabilities and additional digital mode support.

### Supported Radios

- TYT MD-UV380 / MD-UV390 / MD-UV390 Plus
- Retevis RT-3S
- Baofeng DM-1701 / Retevis RT-84

### Project Status

The firmware is stable and well-tested. It provides:
- **DMR** audio transmission and reception
- **M17** digital mode (Open standard alternative to DMR, no proprietary codec required)
- **FM** analog audio transmission and reception
- **DMR hotspot mode** support
- **AES-256 encryption** for encrypted voice communication (experimental)
- **GPS waypoint system** for coordinate tracking
- Additional UI enhancements and bug fixes

Primary testing has been on the TYT MD-UV390 5W, but should work on all listed compatible radios.

## ⚠️ Important Warnings

### Firmware Size & Storage
**This firmware is significantly larger than the base OpenGD77** due to added features including AES-256 encryption library, M17 codec, and enhanced UI. Available flash memory on supported radios is limited. Carefully verify your radio's specifications before flashing. Some radios may have limited flash available for user codeplug data after loading this firmware.

### Legal Considerations - Read Carefully
- **Encryption legality varies by jurisdiction.** AES-256 encryption is provided for testing on shielded/test setups and commercial spectrum only. Using encryption on amateur radio frequencies is **prohibited in most jurisdictions** (including the USA, Canada, and most European nations). Violating these regulations can result in significant legal penalties.
- **M17 is an open-source digital mode** requiring no proprietary licenses, unlike DMR. However, regulatory approval and licensing requirements still apply depending on your location and use case.
- **Users are entirely responsible for compliance** with local regulations. Neither the developer nor the OpenGD77 project assumes any liability for user actions or regulatory violations.

## Key Features vs OpenGD77

### What's Added (Johnny Bravo Enhancements)
1. **AES-256-CTR Encryption**
   - 16-slot key bank with passphrase (T9 multi-tap) or direct hex key entry
   - Per-channel encryption key selection
   - Real-time encrypt/decrypt of AMBE+2 voice frames
   - "ENC" indicator on main screen when active
   - **Bench testing only** — not for use on amateur frequencies

2. **M17 Digital Mode**
   - Open-standard digital mode (no proprietary codec)
   - Uses same mode selection interface as DMR/FM
   - Full integration with existing UI and menus
   - Reduces dependence on proprietary DMR infrastructure

3. **GPS Waypoints**
   - New GPS submenu with waypoint tracking page
   - Ability to save and review GPS coordinates
   - Travel distance calculation between waypoints

4. **UI & Bug Fixes**
   - SK key combinations for call signals and mode cycling
   - ENC warning positioning and display improvements
   - Fixed M17 mode persistence across channel reloads
   - Memory optimizations (Viterbi traceback reduced 4KB→500B)
   - Moved large M17/Codec2 buffers to CCMRAM to prevent overflow

### What's the Same (OpenGD77 Base)
- Core DMR functionality and architecture
- FM analog mode support
- Hotspot mode
- Codeplug system and compatibility
- Menu system and UI paradigm
- All OpenGD77 standard features and contacts/zones management
- AMBE codec integration (provided by CPS loader)
- Flashing method and procedure

### What's NOT Supported
- SMS/text message sending and receiving (OpenGD77 limitation)
- AMBE codec is not included in source (must be provided by OpenGD77 CPS or firmware loader)
- GD-77 support (firmware is too large for GD-77's limited flash)

## Building the Firmware

### Prerequisites
- **Windows 10 or 11**
- **STM32CubeIDE** (2.1+) — download from [st.com](https://www.st.com/en/development-tools/stm32cubeide.html)
- **Python 3.9+** (on system PATH) — used by the patcher
- **OpenGD77 MDUV380 source tree** — download the official `.zip` from [opengd77.com](https://opengd77.com)

### Step 1: Prepare the Source Tree

Extract the official OpenGD77 MDUV380 source `.zip` file. The `Patcher` folder in this repository contains a setup script that will automatically apply all enhancements.

```powershell
cd path\to\sneaky390-repo
powershell -ExecutionPolicy Bypass -File .\Patcher\setup.ps1 `
    -Repo C:\path\to\OpenGD77_MDUV380_DM1701_YYYYMMDD
```

Replace the path with your extracted OpenGD77 source directory.

This script will:
1. Generate the AMBE codec placeholder (`codec_bin_section_1.bin`)
2. Copy encryption and UI source files into the firmware tree
3. Run the patcher to integrate changes into existing source files
4. Report any manual changes needed in `AES_PATCH_TODO.md`

### Step 2: Generate AMBE Placeholder (if not done by setup.ps1)

If you're building manually without the patcher, you must first generate the AMBE codec placeholder:

```cmd
cd MDUV380_firmware
..\prepare.bat
```

This creates a ~298 KB zero-filled placeholder at `MDUV380_firmware\application\source\linkerdata\codec_bin_section_1.bin`. The actual AMBE codec will be injected by the CPS firmware loader during flashing.

### Step 3: Build in STM32CubeIDE

1. Open **STM32CubeIDE**
2. **File → Open Projects from File System** → Select the `MDUV380_firmware` folder
3. **Project → Clean…**
4. **Project → Build Project**
5. Wait for build to complete (watch for any errors in the Console tab)
6. The compiled firmware outputs to the build configuration folder:
   - `.sgl` file (recommended for CPS loader)
   - `.bin` and `.elf` files

### Step 4: Flash the Firmware

1. Put your radio in **firmware update mode** (usually: hold down Power + PTT + SK1 while powering on, or consult your radio's manual)
2. Connect radio to Windows PC via USB
3. Open **OpenGD77 CPS** (Configuration Programming Software)
4. **Extras → Firmware Loader** (menu name may vary by CPS version)
5. Select the `.sgl` file from the build output
6. If prompted, also provide the donor `.bin` (the original firmware .bin from the radio or OpenGD77)
7. Click **Write** and wait for "Firmware update complete"
8. **Power cycle the radio** (off and on)

### Build Troubleshooting

**Linker error: "file not found: codec_bin_section_1.bin"**
- Run `prepare.bat` to generate the AMBE placeholder (see Step 2)

**Linker warning: "Unknown destination type (ARM/Thumb)"**
- Normal with GCC 14.x (STM32CubeIDE 2.1+). The patcher includes a fix for this; if manual build, check `codec_interface.c`

**Build configuration selector**
- STM32CubeIDE may default to Debug. Switch to **Release** for smaller, faster firmware. The build output folder name reflects the configuration (e.g., `Release/`, `Debug/`)

## User Guide

For general OpenGD77 functionality, UI navigation, contacts management, and zones, see the official [OpenGD77 User Guide](https://github.com/LibreDMR/OpenGD77_UserGuide).

### Enhanced Features Quick Start

**Encryption Keys**
1. **Menu → Credits** (second "Credits" entry) → Opens 16-slot encryption key list
2. **Green** on an empty slot → Enter T9 passphrase (e.g., "TEST") or **Right** to enter 64-char hex
3. **Green** to save
4. In **Channel details**, scroll to "Enc Key" and set to your slot number
5. The "ENC" badge appears on the main screen when that channel is active

**M17 Mode**
- Use **SK2 + STAR** (in VFO mode) or standard mode selector on digital channels to cycle through DMR ↔ M17 ↔ FM
- M17 appears in channel details as a selectable mode like DMR
- No additional configuration needed beyond mode selection

**GPS Waypoints**
- **Menu → GPS → Waypoints** to view saved coordinates
- Coordinates are logged automatically when GPS is enabled

## Copyright & Licensing

This firmware is distributed under the same BSD-3-Clause license as OpenGD77 (see `license.txt`). All source code modifications are provided without warranty.

**Key contributors:**
- **Johnny Bravo** — AES-256 encryption, M17 integration, GPS waypoints, UI enhancements, and memory optimizations
- **OpenGD77 team** — Base firmware architecture and DMR/FM implementation

For details on OpenGD77 original contributors and licensing, see the official [OpenGD77 repository](https://github.com/rogerclarkmelbourne/OpenGD77).
	

