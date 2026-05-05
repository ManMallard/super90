#!/usr/bin/env python3
"""
apply_mods.py — applies AES-256 encryption patch to OpenGD77/OpenMDUV380
                source tree (STM32F4 build, MDUV380_firmware/application/).

Idempotent: each modification checks for a sentinel marker before inserting,
so re-running this on a partially-patched tree won't duplicate anything.

Anything that can't be patched safely is appended as a manual TODO to
AES_PATCH_TODO.md in the repo root.

Usage:
    python apply_mods.py <repo_root>
"""
from __future__ import annotations

import io
import os
import re
import sys
from pathlib import Path
from typing import Callable

REPO: Path
APP: Path           # MDUV380_firmware/application
SRC: Path           # MDUV380_firmware/application/source
INC: Path           # MDUV380_firmware/application/include
TODO_PATH: Path
todo_lines: list[str] = []


def todo(msg: str) -> None:
    todo_lines.append(f"- {msg}")
    print(f"  [TODO] {msg}")


def write_todo() -> None:
    if not todo_lines:
        if TODO_PATH.exists():
            TODO_PATH.unlink()
        return
    body = "# AES Patch — Manual Edits Required\n\n"
    body += "The automated patcher could not safely apply the items below.\n"
    body += "Apply them by hand, then rebuild.\n\n"
    body += "\n".join(todo_lines) + "\n"
    TODO_PATH.write_text(body, encoding="utf-8")


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")


def write(p: Path, text: str) -> None:
    p.write_text(text, encoding="utf-8")


def insert_once(text: str, anchor: str, payload: str, sentinel: str) -> str:
    """Insert payload after the line containing anchor, unless sentinel is
    already present anywhere in text. Returns modified text."""
    if sentinel in text:
        return text
    idx = text.find(anchor)
    if idx < 0:
        raise LookupError(f"anchor not found: {anchor[:80]!r}")
    line_end = text.find("\n", idx)
    if line_end < 0:
        line_end = len(text)
    return text[:line_end + 1] + payload + text[line_end + 1:]


def insert_before_once(text: str, anchor: str, payload: str, sentinel: str) -> str:
    """Insert payload immediately before the start of the line containing
    anchor, unless sentinel is present."""
    if sentinel in text:
        return text
    idx = text.find(anchor)
    if idx < 0:
        raise LookupError(f"anchor not found: {anchor[:80]!r}")
    line_start = text.rfind("\n", 0, idx) + 1
    return text[:line_start] + payload + text[line_start:]


# ---------------------------------------------------------------------------
# 1. codeplug.h — rename _UNUSED_2 to encKeyIndex (in CodeplugChannel_t only)
# ---------------------------------------------------------------------------
def mod_codeplug() -> None:
    p = INC / "functions" / "codeplug.h"
    if not p.exists():
        todo(f"codeplug.h not found at {p} — add `uint8_t encKeyIndex;` to CodeplugChannel_t")
        return
    src = read(p)
    if "encKeyIndex" in src:
        print("  codeplug.h: already patched")
        return
    # Find CodeplugChannel_t struct body and rename the _UNUSED_2 inside it.
    m = re.search(r"typedef\s+struct\s*\{[^}]*?\}\s*CodeplugChannel_t\s*;", src, re.S)
    if not m:
        todo("codeplug.h: could not locate CodeplugChannel_t struct - add `uint8_t encKeyIndex;` manually")
        return
    body = m.group(0)
    if "_UNUSED_2" not in body:
        todo("codeplug.h: CodeplugChannel_t no longer has _UNUSED_2 - rename a different reserved byte to encKeyIndex")
        return
    new_body = body.replace(
        "uint8_t\t\t_UNUSED_2;",
        "uint8_t\t\tencKeyIndex; /* AES patch: 0=off, 1..16=key slot */",
        1,
    )
    if new_body == body:
        # Try without tabs
        new_body = body.replace(
            "uint8_t _UNUSED_2;",
            "uint8_t encKeyIndex; /* AES patch: 0=off, 1..16=key slot */",
            1,
        )
    if new_body == body:
        todo("codeplug.h: _UNUSED_2 not found in expected form - rename a reserved byte to encKeyIndex manually")
        return
    src = src[:m.start()] + new_body + src[m.end():]
    write(p, src)
    print("  codeplug.h: renamed _UNUSED_2 -> encKeyIndex")


# ---------------------------------------------------------------------------
# 2. HR-C6000.c — encrypt/decrypt taps around audio-frame SPI calls
# ---------------------------------------------------------------------------
HRC_HEADER = """\
#include "crypto/dmr_crypto.h"
"""

HRC_TX_LOCAL = """\
\t\t\t\t\t\t/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (local) */
\t\t\t\t\t\tif (dmr_crypto_tx_active() && currentChannelData != NULL
\t\t\t\t\t\t    && currentChannelData->chMode == RADIO_MODE_DIGITAL)
\t\t\t\t\t\t{
\t\t\t\t\t\t\tstatic uint32_t txSuperframeNumber = 0;
\t\t\t\t\t\t\tdmr_crypto_tx_frame((uint8_t *)hrc.deferredUpdateBufferOutPtr,
\t\t\t\t\t\t\t                    txSuperframeNumber++);
\t\t\t\t\t\t}
"""

HRC_TX_HOTSPOT = """\
\t\t\t\t\t\t/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (hotspot) */
\t\t\t\t\t\tif (dmr_crypto_tx_active() && currentChannelData != NULL
\t\t\t\t\t\t    && currentChannelData->chMode == RADIO_MODE_DIGITAL)
\t\t\t\t\t\t{
\t\t\t\t\t\t\tstatic uint32_t txSuperframeNumberHS = 0;
\t\t\t\t\t\t\tdmr_crypto_tx_frame((uint8_t *)(deferredUpdateBuffer + LC_DATA_LENGTH),
\t\t\t\t\t\t\t                    txSuperframeNumberHS++);
\t\t\t\t\t\t}
"""

HRC_RX = """\
\t\t\t\t/* AES patch: decrypt 27-byte AMBE voice payload right after chip RX */
\t\t\t\tif (dmr_crypto_rx_active() && currentChannelData != NULL
\t\t\t\t    && currentChannelData->chMode == RADIO_MODE_DIGITAL)
\t\t\t\t{
\t\t\t\t\tstatic uint32_t rxSuperframeNumber = 0;
\t\t\t\t\tdmr_crypto_rx_frame(DMR_frame_buffer + LC_DATA_LENGTH, rxSuperframeNumber++);
\t\t\t\t}
"""


def mod_hrc6000() -> None:
    p = SRC / "hardware" / "HR-C6000.c"
    if not p.exists():
        todo(f"HR-C6000.c not found at {p}")
        return
    src = read(p)
    changed = False

    # Add include after the last existing #include line near the top
    if "#include \"crypto/dmr_crypto.h\"" not in src:
        m = list(re.finditer(r'^#include\s+["<][^"\s>]+["<>]\s*$', src, re.M))
        if not m:
            todo("HR-C6000.c: no #include lines found near top")
        else:
            last = m[-1]
            insert_at = last.end()
            src = src[:insert_at] + "\n" + HRC_HEADER.rstrip() + src[insert_at:]
            changed = True
            print("  HR-C6000.c: added #include crypto/dmr_crypto.h")

    # Tap 1: TX local — line containing the local-TX SPI write
    tx_local_anchor = (
        "SPI1WritePageRegByteArray(0x03, 0x00, "
        "(uint8_t*)hrc.deferredUpdateBufferOutPtr, AMBE_AUDIO_LENGTH)"
    )
    if "/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (local) */" not in src:
        try:
            src = insert_before_once(
                src, tx_local_anchor, HRC_TX_LOCAL,
                "/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (local) */",
            )
            changed = True
            print("  HR-C6000.c: TX local tap inserted")
        except LookupError:
            todo(f"HR-C6000.c: TX local SPI call not found - insert encrypt block before line containing: {tx_local_anchor}")

    # Tap 2: TX hotspot
    tx_hotspot_anchor = (
        "SPI1WritePageRegByteArray(0x03, 0x00, "
        "(uint8_t*)(deferredUpdateBuffer + LC_DATA_LENGTH), AMBE_AUDIO_LENGTH)"
    )
    if "/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (hotspot) */" not in src:
        try:
            src = insert_before_once(
                src, tx_hotspot_anchor, HRC_TX_HOTSPOT,
                "/* AES patch: encrypt 27-byte AMBE voice payload before chip TX (hotspot) */",
            )
            changed = True
            print("  HR-C6000.c: TX hotspot tap inserted")
        except LookupError:
            todo(f"HR-C6000.c: TX hotspot SPI call not found")

    # Tap 3: RX
    rx_anchor = (
        "SPI1ReadPageRegByteArray(0x03, 0x00, "
        "DMR_frame_buffer + LC_DATA_LENGTH, AMBE_AUDIO_LENGTH)"
    )
    if "/* AES patch: decrypt 27-byte AMBE voice payload right after chip RX */" not in src:
        try:
            src = insert_once(
                src, rx_anchor, HRC_RX,
                "/* AES patch: decrypt 27-byte AMBE voice payload right after chip RX */",
            )
            changed = True
            print("  HR-C6000.c: RX tap inserted")
        except LookupError:
            todo(f"HR-C6000.c: RX SPI call not found")

    if changed:
        write(p, src)


# ---------------------------------------------------------------------------
# 3. menuSystem.h — add MENU_KEY_MANAGEMENT, MENU_KEY_ENTRY enum entries
#                  and the two function prototypes
# ---------------------------------------------------------------------------
def mod_menuSystem_h() -> None:
    p = INC / "user_interface" / "menuSystem.h"
    if not p.exists():
        todo(f"menuSystem.h not found at {p}")
        return
    src = read(p)
    changed = False

    # Add enum entries before NUM_MENU_ENTRIES
    if "MENU_KEY_MANAGEMENT" not in src:
        anchor = "NUM_MENU_ENTRIES"
        try:
            src = insert_before_once(
                src, anchor,
                "\tMENU_KEY_MANAGEMENT,\n\tMENU_KEY_ENTRY,\n",
                "MENU_KEY_MANAGEMENT",
            )
            changed = True
            print("  menuSystem.h: added MENU_KEY_MANAGEMENT and MENU_KEY_ENTRY")
        except LookupError:
            todo("menuSystem.h: could not find NUM_MENU_ENTRIES - add MENU_KEY_MANAGEMENT and MENU_KEY_ENTRY to MENU_SCREENS enum manually")

    # Add prototypes
    if "menuKeyManagement(" not in src:
        # Anchor on a known existing prototype.  STM32 fork uses "event"
        # for the parameter name, Kinetis fork uses "ev" — accept either.
        for proto_anchor in (
            "menuStatus_t menuChannelDetails(uiEvent_t *event, bool isFirstRun);",
            "menuStatus_t menuChannelDetails(uiEvent_t *ev, bool isFirstRun);",
        ):
            if proto_anchor in src:
                src = insert_once(
                    src, proto_anchor,
                    "menuStatus_t menuKeyManagement(uiEvent_t *event, bool isFirstRun);\n"
                    "menuStatus_t menuKeyEntry(uiEvent_t *event, bool isFirstRun);\n",
                    "menuKeyManagement(uiEvent_t",
                )
                changed = True
                print("  menuSystem.h: added prototypes for menuKeyManagement and menuKeyEntry")
                break
        else:
            todo("menuSystem.h: anchor menuChannelDetails prototype not found - add menuKeyManagement and menuKeyEntry prototypes manually")

    if changed:
        write(p, src)


# ---------------------------------------------------------------------------
# 4. menuSystem.c — add { menuKeyManagement, NULL, NULL, 0 } and
#                  { menuKeyEntry, NULL, NULL, 0 } to menuFunctions[]
#                  and { 2, MENU_KEY_MANAGEMENT } to mainMenuItems[]
# ---------------------------------------------------------------------------
def mod_menuSystem_c() -> None:
    p = SRC / "user_interface" / "menuSystem.c"
    if not p.exists():
        todo(f"menuSystem.c not found at {p}")
        return
    src = read(p)
    changed = False

    # ---- 4a. menuFunctions[] — append two entries at the end ----
    if "{ menuKeyManagement," not in src:
        # Find the closing brace of menuFunctions[] array.
        m = re.search(
            r"static\s+menuFunctionData_t\s+menuFunctions\s*\[\s*\]\s*=\s*\{",
            src,
        )
        if not m:
            todo("menuSystem.c: could not locate menuFunctions[] array")
        else:
            # Walk forward to find the matching closing brace.
            depth = 1
            i = m.end()
            while i < len(src) and depth > 0:
                if src[i] == "{":
                    depth += 1
                elif src[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            if depth != 0:
                todo("menuSystem.c: brace match failed in menuFunctions[]")
            else:
                # Look back to before the closing brace and insert before it.
                # Find the last existing entry's line — preserve trailing comma.
                close = i
                # Find start of line containing the close brace
                line_start = src.rfind("\n", 0, close) + 1
                payload = (
                    "\t\t{ menuKeyManagement,      NULL, NULL, 0 },\n"
                    "\t\t{ menuKeyEntry,           NULL, NULL, 0 },\n"
                )
                src = src[:line_start] + payload + src[line_start:]
                changed = True
                print("  menuSystem.c: appended menuKeyManagement and menuKeyEntry to menuFunctions[]")

    # ---- 4b. mainMenuItems[] — insert before its closing brace ----
    if "MENU_KEY_MANAGEMENT" not in src:
        m = re.search(
            r"const\s+menuItemNewData_t\s+mainMenuItems\s*\[\s*\]\s*=\s*\{",
            src,
        )
        if not m:
            todo("menuSystem.c: could not locate mainMenuItems[] array")
        else:
            depth = 1
            i = m.end()
            while i < len(src) and depth > 0:
                if src[i] == "{":
                    depth += 1
                elif src[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            if depth != 0:
                todo("menuSystem.c: brace match failed in mainMenuItems[]")
            else:
                close = i
                line_start = src.rfind("\n", 0, close) + 1
                payload = (
                    "\t{   2, MENU_KEY_MANAGEMENT  }, /* AES patch: shows \"Credits\" placeholder */\n"
                )
                src = src[:line_start] + payload + src[line_start:]
                changed = True
                print("  menuSystem.c: appended MENU_KEY_MANAGEMENT to mainMenuItems[]")

    if changed:
        write(p, src)


# ---------------------------------------------------------------------------
# 5. menuChannelDetails.c — Block C (enum), D (render), E (LEFT/RIGHT)
# ---------------------------------------------------------------------------
def mod_menuChannelDetails() -> None:
    p = SRC / "user_interface" / "menuChannelDetails.c"
    if not p.exists():
        todo(f"menuChannelDetails.c not found at {p}")
        return
    src = read(p)
    changed = False

    # ---- 5a. Add include for crypto/key_storage.h ----
    if "#include \"crypto/key_storage.h\"" not in src:
        m = list(re.finditer(r'^#include\s+["<][^"\s>]+["<>]\s*$', src, re.M))
        if m:
            last = m[-1]
            src = src[:last.end()] + "\n#include \"crypto/key_storage.h\"" + src[last.end():]
            changed = True
            print("  menuChannelDetails.c: added #include crypto/key_storage.h")

    # ---- 5b. Block C — add CH_DETAILS_ENC_KEY before NUM_CH_DETAILS_ITEMS ----
    if "CH_DETAILS_ENC_KEY" not in src:
        anchor = "NUM_CH_DETAILS_ITEMS"
        try:
            src = insert_before_once(
                src, anchor,
                "\tCH_DETAILS_ENC_KEY,\n",
                "CH_DETAILS_ENC_KEY",
            )
            changed = True
            print("  menuChannelDetails.c: added CH_DETAILS_ENC_KEY enum entry")
        except LookupError:
            todo("menuChannelDetails.c: could not find NUM_CH_DETAILS_ITEMS - add CH_DETAILS_ENC_KEY before it manually")

    # ---- 5c. Block D — render case in updateScreen()'s switch ----
    if "/* AES patch: CH_DETAILS_ENC_KEY render */" not in src:
        # Locate updateScreen() definition (not its forward declaration).
        # The function body begins at the first `{` after a line like:
        #   `static void updateScreen(bool isFirstRun, bool allowedToSpeakUpdate)`
        # (no trailing semicolon).
        m = re.search(
            r"static\s+void\s+updateScreen\s*\([^)]*\)\s*\{",
            src,
        )
        if not m:
            todo("menuChannelDetails.c: updateScreen() function not found")
        else:
            # Brace-match to find end of updateScreen.
            depth = 1
            i = m.end()
            while i < len(src) and depth > 0:
                if src[i] == "{":
                    depth += 1
                elif src[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            if depth != 0:
                todo("menuChannelDetails.c: brace match failed in updateScreen()")
            else:
                fn_body_start = m.end()
                fn_body_end = i
                # The render switch in updateScreen() uses `switch(mNum)`
                # (where mNum = current visible-item offset). Some forks
                # use `switch (menuDataGlobal.currentItemIndex)` instead.
                # Find the LARGEST switch in this function — that's the
                # render switch with all CH_DETAILS_* cases.
                fn_body = src[fn_body_start:fn_body_end]
                candidates = []
                for sw in re.finditer(
                    r"switch\s*\(\s*(?:mNum|menuDataGlobal\.currentItemIndex)\s*\)\s*\{",
                    fn_body,
                ):
                    sw_open_end_abs = fn_body_start + sw.end()
                    depth2 = 1
                    j = sw_open_end_abs
                    while j < len(src) and depth2 > 0:
                        if src[j] == "{":
                            depth2 += 1
                        elif src[j] == "}":
                            depth2 -= 1
                            if depth2 == 0:
                                break
                        j += 1
                    if depth2 == 0:
                        candidates.append((sw, sw_open_end_abs, j, j - sw_open_end_abs))
                if not candidates:
                    todo("menuChannelDetails.c: render switch not found in updateScreen()")
                else:
                    # Pick the largest — that's the actual render switch.
                    candidates.sort(key=lambda t: -t[3])
                    sw, sw_open_end_abs, close_idx, _size = candidates[0]
                    line_start = src.rfind("\n", 0, close_idx) + 1
                    indent_match = re.match(r"[\t ]*", src[line_start:close_idx])
                    indent = indent_match.group(0) if indent_match else "\t\t"
                    payload = (
                        f"{indent}case CH_DETAILS_ENC_KEY:\n"
                        f"{indent}\t/* AES patch: CH_DETAILS_ENC_KEY render */\n"
                        f"{indent}\t{{\n"
                        f"{indent}\t\tstatic const char encKeyLabel[] = \"Enc Key\";\n"
                        f"{indent}\t\tleftSide = encKeyLabel;\n"
                        f"{indent}\t\tif (tmpChannel.chMode != RADIO_MODE_DIGITAL)\n"
                        f"{indent}\t\t{{\n"
                        f"{indent}\t\t\trightSideConst = currentLanguage->n_a;\n"
                        f"{indent}\t\t}}\n"
                        f"{indent}\t\telse if (tmpChannel.encKeyIndex == 0)\n"
                        f"{indent}\t\t{{\n"
                        f"{indent}\t\t\tstrcpy(rightSideVar, \"Off\");\n"
                        f"{indent}\t\t}}\n"
                        f"{indent}\t\telse\n"
                        f"{indent}\t\t{{\n"
                        f"{indent}\t\t\tconst KeySlot_t *ks = keystore_get(tmpChannel.encKeyIndex);\n"
                        f"{indent}\t\t\tif (ks && (ks->flags & KEY_FLAG_SET) && ks->label[0])\n"
                        f"{indent}\t\t\t{{\n"
                        f"{indent}\t\t\t\tint n = (KEY_LABEL_LEN < (int)sizeof(rightSideVar) - 1)\n"
                        f"{indent}\t\t\t\t        ? KEY_LABEL_LEN : (int)sizeof(rightSideVar) - 1;\n"
                        f"{indent}\t\t\t\tmemcpy(rightSideVar, ks->label, (size_t)n);\n"
                        f"{indent}\t\t\t\trightSideVar[n] = 0;\n"
                        f"{indent}\t\t\t}}\n"
                        f"{indent}\t\t\telse\n"
                        f"{indent}\t\t\t{{\n"
                        f"{indent}\t\t\t\tsnprintf(rightSideVar, sizeof(rightSideVar), \"Slot %u\",\n"
                        f"{indent}\t\t\t\t         (unsigned)tmpChannel.encKeyIndex);\n"
                        f"{indent}\t\t\t}}\n"
                        f"{indent}\t\t}}\n"
                        f"{indent}\t}}\n"
                        f"{indent}\tbreak;\n"
                    )
                    src = src[:line_start] + payload + src[line_start:]
                    changed = True
                    print("  menuChannelDetails.c: added Block D (render case)")

    # ---- 5d. Block E — LEFT and RIGHT handler cases ----
    # Anchor on the unique `handlesLeftKey:` / `handlesRightKey:` goto
    # labels. The switch on currentItemIndex is the next switch after
    # each label. Earlier KEY_LEFT/RIGHT references (e.g. the freq-entry
    # backspace handler) don't have these labels, so this disambiguates.
    def patch_handler_switch(src_text: str, label: str, sentinel: str,
                             body_for: Callable[[str], str]) -> tuple[str, bool]:
        if sentinel in src_text:
            return src_text, False
        # Find the label declaration (not the goto reference). Use the
        # specific pattern `\n<whitespace>label:\n` to skip the gotos.
        m = re.search(rf"\n[\t ]+{label}:\s*\n", src_text)
        if not m:
            return src_text, False
        # Find next switch after the label
        sw = re.search(
            r"switch\s*\(\s*menuDataGlobal\.currentItemIndex\s*\)\s*\{",
            src_text[m.end():],
        )
        if not sw:
            return src_text, False
        sw_open_end = m.end() + sw.end()
        # Brace-match to find the closing brace of the switch
        depth = 1
        i = sw_open_end
        while i < len(src_text) and depth > 0:
            if src_text[i] == "{":
                depth += 1
            elif src_text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if depth != 0:
            return src_text, False
        close_idx = i
        line_start = src_text.rfind("\n", 0, close_idx) + 1
        indent_match = re.match(r"[\t ]*", src_text[line_start:close_idx])
        indent = indent_match.group(0) if indent_match else "\t\t\t"
        return (
            src_text[:line_start] + body_for(indent) + src_text[line_start:],
            True,
        )

    def left_body(indent: str) -> str:
        return (
            f"{indent}case CH_DETAILS_ENC_KEY:\n"
            f"{indent}\t/* AES patch: CH_DETAILS_ENC_KEY LEFT */\n"
            f"{indent}\tif (tmpChannel.encKeyIndex > 0) tmpChannel.encKeyIndex--;\n"
            f"{indent}\tbreak;\n"
        )

    def right_body(indent: str) -> str:
        return (
            f"{indent}case CH_DETAILS_ENC_KEY:\n"
            f"{indent}\t/* AES patch: CH_DETAILS_ENC_KEY RIGHT */\n"
            f"{indent}\tif (tmpChannel.encKeyIndex < KEY_SLOT_COUNT) tmpChannel.encKeyIndex++;\n"
            f"{indent}\tbreak;\n"
        )

    src, ok_left = patch_handler_switch(
        src, "handlesLeftKey",
        "/* AES patch: CH_DETAILS_ENC_KEY LEFT */",
        left_body,
    )
    src, ok_right = patch_handler_switch(
        src, "handlesRightKey",
        "/* AES patch: CH_DETAILS_ENC_KEY RIGHT */",
        right_body,
    )
    if ok_left:
        changed = True
        print("  menuChannelDetails.c: added Block E (LEFT handler case)")
    elif "/* AES patch: CH_DETAILS_ENC_KEY LEFT */" not in src:
        todo("menuChannelDetails.c: KEY_LEFT handler switch not found - add CH_DETAILS_ENC_KEY case manually")

    if ok_right:
        changed = True
        print("  menuChannelDetails.c: added Block E (RIGHT handler case)")
    elif "/* AES patch: CH_DETAILS_ENC_KEY RIGHT */" not in src:
        todo("menuChannelDetails.c: KEY_RIGHT handler switch not found - add CH_DETAILS_ENC_KEY case manually")

    if changed:
        write(p, src)


# ---------------------------------------------------------------------------
# 6. uiUtilities.c — call enc_indicator_render() from main-screen header
# ---------------------------------------------------------------------------
def mod_uiUtilities() -> None:
    p = SRC / "user_interface" / "uiUtilities.c"
    if not p.exists():
        todo(f"uiUtilities.c not found at {p} - call enc_indicator_render() from main screen render path manually")
        return
    src = read(p)
    if "enc_indicator_render" in src:
        return
    if "#include \"crypto/enc_indicator.h\"" not in src:
        m = list(re.finditer(r'^#include\s+["<][^"\s>]+["<>]\s*$', src, re.M))
        if not m:
            todo("uiUtilities.c: no #include lines found")
            return
        last = m[-1]
        src = src[:last.end()] + "\n#include \"crypto/enc_indicator.h\"" + src[last.end():]
    # Best-effort placement: append to a function called something like
    # uiUtilityRenderHeader or similar.  If we can't find one, leave a TODO.
    candidates = [
        "uiUtilityRenderHeader",
        "uiUtilityDrawRSSIBarGraph",
    ]
    inserted = False
    for fn in candidates:
        m = re.search(rf"\b{fn}\b\s*\([^)]*\)\s*\{{", src)
        if not m:
            continue
        # Find function's closing brace.
        depth = 1
        i = m.end()
        while i < len(src) and depth > 0:
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if depth != 0:
            continue
        line_start = src.rfind("\n", 0, i) + 1
        src = src[:line_start] + "\tenc_indicator_render();\n" + src[line_start:]
        write(p, src)
        print(f"  uiUtilities.c: enc_indicator_render() added to {fn}()")
        inserted = True
        break
    if not inserted:
        write(p, src)
        todo("uiUtilities.c: place a call to enc_indicator_render() in the function that draws the main-screen header (typically uiUtilityRenderHeader)")


# ---------------------------------------------------------------------------
# 8. codec_interface.c + codec.h — replace `BL <literal>` with `LDR/BLX <reg>`
#    so newer GCC/binutils (14.x+) can link the absolute calls into the
#    AMBE codec. Independent of the AES patch itself, but required to
#    build at all on STM32CubeIDE 2.1+.
# ---------------------------------------------------------------------------
def mod_codec_blx_fix() -> None:
    # 8a. codec.h — OR bit 0 into the address macros, drop trailing semicolons.
    h = INC / "dmr_codec" / "codec.h"
    if h.exists():
        src = read(h)
        if "/* AES patch: BLX fix */" not in src:
            patched = src
            # Match either `#define NAME 0xADDR;` or `#define NAME 0xADDR`
            for sym, orig_lsb_clear, orig_lsb_set in [
                ("AMBE_DECODE",     "0x08075954", "0x08075955"),
                ("AMBE_ENCODE",     "0x080754ac", "0x080754ad"),
                ("AMBE_ENCODE_ECC", "0x08075864", "0x08075865"),
            ]:
                # Tolerate trailing semicolon and surrounding spaces.
                pat = re.compile(
                    rf"(#define\s+{sym}\s+){re.escape(orig_lsb_clear)};?",
                )
                patched = pat.sub(rf"\g<1>{orig_lsb_set}    /* AES patch: BLX fix */", patched)
            if patched != src:
                write(h, patched)
                print("  codec.h: AMBE_DECODE/ENCODE/ENCODE_ECC addresses patched (Thumb bit set, trailing ; removed)")
            else:
                # Maybe the address was already patched in some other variant
                print("  codec.h: no matching addresses to patch (already fixed?)")

    # 8b. codec_interface.c — replace BL <literal> with LDR R12, =<literal>; BLX R12.
    c = SRC / "dmr_codec" / "codec_interface.c"
    if not c.exists():
        return
    src = read(c)
    if "/* AES patch: BLX fix */" in src:
        return
    # Replace every `"BL " QU(NAME)` with the LDR/BLX form.
    # Be tolerant of `\n\t\t\t\"BL \" QU(...)` indentation — only the
    # `"BL " QU(...)` substring matters for the regex.
    pattern = re.compile(r'"BL "\s+QU\((AMBE_(?:DECODE|ENCODE|ENCODE_ECC))\)')
    count = 0
    def repl(m):
        nonlocal count
        count += 1
        sym = m.group(1)
        return (f'"LDR R12, =" QU({sym}) "\\n"\n\t\t\t"BLX R12\\n"   /* AES patch: BLX fix */')
    new_src = pattern.sub(repl, src)
    if count == 0:
        print("  codec_interface.c: no BL <literal> calls found — already patched?")
        return
    write(c, new_src)
    print(f"  codec_interface.c: replaced {count} BL <literal> call(s) with LDR/BLX")
def verify_new_files() -> None:
    expected = [
        SRC / "crypto" / "aes.c",
        SRC / "crypto" / "sha256.c",
        SRC / "crypto" / "kdf.c",
        SRC / "crypto" / "dmr_crypto.c",
        SRC / "crypto" / "key_storage.c",
        SRC / "crypto" / "enc_indicator.c",
        SRC / "user_interface" / "menuKeyManagement.c",
        SRC / "user_interface" / "menuKeyEntry.c",
        INC / "crypto" / "aes.h",
        INC / "crypto" / "sha256.h",
        INC / "crypto" / "kdf.h",
        INC / "crypto" / "dmr_crypto.h",
        INC / "crypto" / "key_storage.h",
        INC / "crypto" / "enc_indicator.h",
    ]
    for f in expected:
        if not f.exists():
            todo(f"missing {f.relative_to(REPO)} — copy from new_files/ via setup.ps1")


def main() -> int:
    global REPO, APP, SRC, INC, TODO_PATH
    if len(sys.argv) != 2:
        print("Usage: apply_mods.py <repo_root>", file=sys.stderr)
        return 2
    REPO = Path(sys.argv[1]).resolve()
    APP = REPO / "MDUV380_firmware" / "application"
    SRC = APP / "source"
    INC = APP / "include"
    TODO_PATH = REPO / "AES_PATCH_TODO.md"

    if not APP.exists():
        print(f"ERROR: not an OpenMDUV380 source tree (no MDUV380_firmware/application at {REPO})",
              file=sys.stderr)
        return 1

    print("Applying AES-256 patch to STM32 OpenMDUV380 source tree...")
    print(f"  Repo: {REPO}")
    print(f"  App:  {APP}")
    print()

    verify_new_files()
    mod_codeplug()
    mod_hrc6000()
    mod_menuSystem_h()
    mod_menuSystem_c()
    mod_menuChannelDetails()
    mod_uiUtilities()
    mod_codec_blx_fix()

    write_todo()
    if todo_lines:
        print()
        print(f"  {len(todo_lines)} item(s) require manual edits — see {TODO_PATH.relative_to(REPO)}")
    else:
        print()
        print("  All automated edits applied successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
