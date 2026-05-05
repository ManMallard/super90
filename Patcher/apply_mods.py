#!/usr/bin/env python3
"""
apply_mods.py — anchored text modifications for the AES-256 OpenGD77 patch.

Each operation finds a regex anchor in a target file and either inserts text
before/after the match or replaces a region. Failures print the intended change
so a human can apply it manually.
"""
import os, re, sys, argparse, textwrap

class ModFailed(Exception): pass

def read(p):  return open(p, 'r', encoding='utf-8', errors='replace').read()
def write(p, s): open(p, 'w', encoding='utf-8').write(s)

def insert_after(path, anchor_re, payload, label):
    src = read(path)
    m = re.search(anchor_re, src, re.MULTILINE)
    if not m:
        raise ModFailed(f"[{label}] anchor not found in {path}: /{anchor_re}/")
    if payload.strip() in src:
        print(f"[{label}] already applied, skipping")
        return
    cut = m.end()
    write(path, src[:cut] + "\n" + payload + "\n" + src[cut:])
    print(f"[{label}] inserted into {path}")

def insert_before(path, anchor_re, payload, label):
    src = read(path)
    m = re.search(anchor_re, src, re.MULTILINE)
    if not m:
        raise ModFailed(f"[{label}] anchor not found in {path}: /{anchor_re}/")
    if payload.strip() in src:
        print(f"[{label}] already applied, skipping")
        return
    cut = m.start()
    write(path, src[:cut] + payload + "\n" + src[cut:])
    print(f"[{label}] inserted into {path}")

def replace_region(path, start_re, end_re, payload, label):
    src = read(path)
    s = re.search(start_re, src, re.MULTILINE)
    e = re.search(end_re, src, re.MULTILINE)
    if not s or not e or e.start() <= s.end():
        raise ModFailed(f"[{label}] region not found in {path}")
    if payload.strip() in src:
        print(f"[{label}] already applied, skipping")
        return
    write(path, src[:s.start()] + payload + src[e.end():])
    print(f"[{label}] replaced region in {path}")

# ----------------------------------------------------------------------------
# Modifications
# ----------------------------------------------------------------------------

def find(root, *parts):
    """Search a few common subpaths for a filename."""
    candidates = []
    for dirpath, _, files in os.walk(root):
        for f in files:
            full = os.path.join(dirpath, f)
            if all(p in full for p in parts):
                candidates.append(full)
    if not candidates:
        return None
    candidates.sort(key=len)
    return candidates[0]

def mod_codeplug(root):
    """Add encKeyIndex (1 byte) to struct_codeplugChannel_t.
    Tries several reserved/pad field naming conventions in order.
    """
    p = find(root, 'codeplug.h')
    if not p:
        raise ModFailed("codeplug.h not found")
    src = read(p)
    if 'encKeyIndex' in src:
        print("[codeplug.h] already applied"); return

    # Strategy A: shrink an existing pad/reserved array by 1 and add the field.
    pats = [
        # LibreDMR-style pad
        (r'(\buint8_t\s+LibreDMR_Pad\[)(\d+)(\];)',
         lambda m: f"{m.group(1)}{int(m.group(2))-1}{m.group(3)}\n\tuint8_t encKeyIndex;  /* 0=off, 1..16 = key slot */"),
        # _unused / unused
        (r'(\buint8_t\s+_unused\d*\[)(\d+)(\];)',
         lambda m: f"{m.group(1)}{int(m.group(2))-1}{m.group(3)}\n\tuint8_t encKeyIndex;"),
        (r'(\buint8_t\s+unused\d*\[)(\d+)(\];)',
         lambda m: f"{m.group(1)}{int(m.group(2))-1}{m.group(3)}\n\tuint8_t encKeyIndex;"),
        # reserved
        (r'(\buint8_t\s+reserved\d*\[)(\d+)(\];)',
         lambda m: f"{m.group(1)}{int(m.group(2))-1}{m.group(3)}\n\tuint8_t encKeyIndex;"),
        # pad / padding
        (r'(\buint8_t\s+pad(?:ding)?\d*\[)(\d+)(\];)',
         lambda m: f"{m.group(1)}{int(m.group(2))-1}{m.group(3)}\n\tuint8_t encKeyIndex;"),
    ]
    for pat, repl in pats:
        new, n = re.subn(pat, repl, src, count=1)
        if n:
            write(p, new)
            print(f"[codeplug.h] added encKeyIndex via /{pat[:40]}.../")
            return

    # Strategy B: locate struct_codeplugChannel_t and insert encKeyIndex
    # right before its closing brace. This grows the struct by 1 byte —
    # acceptable when there's no explicit pad to steal from.
    m = re.search(r'\bstruct\s+\{([^{}]*?)\}\s*__attribute__\s*\(\s*\(\s*packed\s*\)\s*\)\s*struct_codeplugChannel_t',
                  src, re.MULTILINE | re.DOTALL)
    if not m:
        m = re.search(r'\btypedef\s+struct\s*\{([^{}]*?)\}\s*__attribute__\s*\(\s*\(\s*packed\s*\)\s*\)\s*struct_codeplugChannel_t',
                      src, re.MULTILINE | re.DOTALL)
    if m:
        body_start = m.start(1)
        body_end   = m.end(1)
        new_field  = "\n\tuint8_t encKeyIndex;  /* AES patch: 0=off, 1..16 = key slot */\n"
        write(p, src[:body_end] + new_field + src[body_end:])
        print(f"[codeplug.h] appended encKeyIndex at end of struct body (struct grew by 1 byte)")
        with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
            f.write(textwrap.dedent("""
            ## codeplug.h — note about struct size change
            No explicit pad/reserved field was found, so `encKeyIndex` was appended
            at the end of `struct_codeplugChannel_t`. The packed struct is now 1
            byte larger. If the firmware reads a fixed channel record length from
            EEPROM, search the codebase for `sizeof(struct_codeplugChannel_t)` or
            a hard-coded channel length constant (often 56 for OpenGD77) and bump
            it by 1. Otherwise existing codeplugs may misalign on first read.
            """))
        return

    raise ModFailed(textwrap.dedent("""\
        codeplug.h: could not locate a pad/reserved field or the struct itself.
        Manual fix: in struct_codeplugChannel_t, reduce one reserved array by 1
        and add:
            uint8_t encKeyIndex;   /* 0=off, 1..16 = key slot */
    """))

def mod_hrc6000_includes(root):
    p = find(root, 'HR-C6000.c')
    if not p:
        raise ModFailed("HR-C6000.c not found")
    src = read(p)
    if 'crypto/dmr_crypto.h' in src:
        print("[HR-C6000.c includes] already applied"); return

    # open-ham/OpenGD77 uses #include "hardware/HR-C6000.h"
    # Other forks may use "functions/HR-C6000.h" or just "HR-C6000.h"
    anchors = [
        r'^#include\s+"hardware/HR-C6000\.h"\s*$',
        r'^#include\s+"functions/HR-C6000\.h"\s*$',
        r'^#include\s+"HR-C6000\.h"\s*$',
        r'^#include\s+"functions/settings\.h"\s*$',   # near top of HR-C6000.c
    ]
    for a in anchors:
        try:
            insert_after(p, a,
                '#include "crypto/dmr_crypto.h"\n#include "crypto/key_storage.h"',
                f'HR-C6000.c includes @ /{a[:40]}/')
            return
        except ModFailed:
            continue
    raise ModFailed("HR-C6000.c: no suitable include anchor found")

def mod_hrc6000_taps(root):
    """
    The actual encrypt/decrypt insertion needs to happen at the AMBE+2 buffer
    boundary. The exact function name varies across OpenGD77 forks. We try
    several known candidates; if none match, we leave a TODO file for the user.
    """
    p = find(root, 'HR-C6000.c')
    src = read(p)
    if 'dmr_crypto_tx_frame' in src:
        print("[HR-C6000.c] taps already present"); return

    # TX tap candidates — open-ham fork: voice frames are pushed via
    # SPI0WritePageRegByte / SPI0WritePageRegByteArray to chip register 0x03
    # inside tick_HR_C6000 / handle_TX_audio paths. We try the tightest
    # patterns first, fall back to broader ones.
    tx_anchor_candidates = [
        r'SPI0WritePageRegByteArray\s*\(\s*0x03\b',          # direct write to TX audio reg
        r'/\*\s*Send.*AMBE.*?\*/',
        r'\bhandle_tx_audio\b',
        r'\btick_TXBuffer\b',
        r'\bhrc6000HandleVoiceFrame\b',
        r'wavbufferReadIdx',
    ]
    rx_anchor_candidates = [
        r'SPI0ReadPageRegByteArray\s*\(\s*0x82\b',           # direct read from RX audio reg
        r'/\*\s*Receive.*AMBE.*?\*/',
        r'\bhandle_rx_audio\b',
        r'\btick_RXBuffer\b',
        r'\bcc_RXFrameAndCheck\b',
        r'wavbufferWriteIdx',
    ]

    placed_tx = placed_rx = False
    for a in tx_anchor_candidates:
        try:
            insert_after(p, a,
                "\t/* AES-256-CTR: encrypt the 27-byte AMBE+2 superframe buffer just\n"
                "\t * before it goes to the HR-C6000 SPI registers. The buffer\n"
                "\t * variable name in your tree may differ — adjust the symbol below. */\n"
                "\tif (dmr_crypto_tx_active() && currentChannelData != NULL\n"
                "\t    && (currentChannelData->chMode == RADIO_MODE_DIGITAL)) {\n"
                "\t    extern uint8_t  audioAndHotspotDataBuffer[];\n"
                "\t    extern uint32_t txSuperframeNumber;\n"
                "\t    dmr_crypto_tx_frame(audioAndHotspotDataBuffer, txSuperframeNumber++);\n"
                "\t}",
                f'HR-C6000.c TX tap @{a[:25]}')
            placed_tx = True
            break
        except ModFailed:
            continue

    for a in rx_anchor_candidates:
        try:
            insert_after(p, a,
                "\t/* AES-256-CTR: decrypt the 27-byte AMBE+2 superframe buffer as soon\n"
                "\t * as it arrives from the HR-C6000 SPI registers. Adjust symbols if\n"
                "\t * the buffer/var names differ in your tree. */\n"
                "\tif (dmr_crypto_rx_active() && currentChannelData != NULL\n"
                "\t    && (currentChannelData->chMode == RADIO_MODE_DIGITAL)) {\n"
                "\t    extern uint8_t  audioAndHotspotDataBuffer[];\n"
                "\t    extern uint32_t rxSuperframeNumber;\n"
                "\t    dmr_crypto_rx_frame(audioAndHotspotDataBuffer, rxSuperframeNumber++);\n"
                "\t}",
                f'HR-C6000.c RX tap @{a[:25]}')
            placed_rx = True
            break
        except ModFailed:
            continue

    if not (placed_tx and placed_rx):
        todo = os.path.join(root, 'AES_PATCH_TODO.md')
        with open(todo, 'a') as f:
            f.write(textwrap.dedent("""
            ## HR-C6000.c manual taps required
            Could not auto-locate the AMBE+2 SPI transfer site. In `HR-C6000.c`:

            **TX:** find the function that writes the 27-byte audio payload to the
            HR-C6000 SPI registers (region around chip register 0x03/0x04).
            Insert *immediately before the SPI write*:

                if (dmr_crypto_tx_active() && currentChannelData != NULL
                    && currentChannelData->chMode == RADIO_MODE_DIGITAL) {
                    dmr_crypto_tx_frame(<your-buf-var>, txSuperframeNumber++);
                }

            **RX:** find the matching read from chip register 0x82.
            Insert *immediately after the SPI read*:

                if (dmr_crypto_rx_active() && currentChannelData != NULL
                    && currentChannelData->chMode == RADIO_MODE_DIGITAL) {
                    dmr_crypto_rx_frame(<your-buf-var>, rxSuperframeNumber++);
                }

            Declare somewhere accessible:

                uint32_t txSuperframeNumber, rxSuperframeNumber;
            """))
        print("[HR-C6000.c] auto-tap failed — see AES_PATCH_TODO.md")

def mod_channel_details(root):
    p = find(root, 'menuChannelDetails.c')
    if not p:
        print("[menuChannelDetails.c] not found, skipping (insert manually)"); return
    insert_after(p,
        r'^#include\s+"user_interface/menuSystem\.h"',
        '#include "crypto/key_storage.h"',
        'menuChannelDetails.c include')
    # Insertion of the new menu line is highly tree-specific; emit TODO instead.
    with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
        f.write(textwrap.dedent("""
        ## menuChannelDetails.c — add Encryption Key selector
        Add a row in the channel-details menu enum/array that lets the user
        pick `currentChannelData->encKeyIndex` in 0..16. The field is already
        in the codeplug struct via the codeplug.h patch.
        On render, look up `keystore_get(idx)` to show the slot label.
        """))

def mod_main_menu(root):
    p = find(root, 'menuMainMenu.c')
    if not p:
        with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
            f.write("\n## menuMainMenu.c not found — register MENU_KEY_MANAGEMENT manually.\n")
        return
    src = read(p)
    if 'menuKeyManagement' in src:
        print("[menuMainMenu.c] already applied"); return
    # Append a TODO marker — splicing into an enum table is fork-specific.
    with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
        f.write(textwrap.dedent("""
        ## menuMainMenu.c — add 'Encryption Keys' top-level entry
        In the main menu items array, add an entry that calls `menuKeyManagement`.
        Also add `extern menuStatus_t menuKeyManagement(uiEvent_t*, bool);`
        to `menuSystem.h` and a MENU_KEY_MANAGEMENT enum slot.
        """))

def mod_enc_indicator(root):
    """
    Inject a call to enc_indicator_draw() into the header rendering function
    on open-ham/OpenGD77. The header is drawn by uiUtilityRenderHeader(),
    typically in user_interface/uiUtilities.c. We insert the call at the very
    end of the function so the badge draws on top of any existing icons.
    """
    p = find(root, 'uiUtilities.c')
    if not p:
        with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
            f.write(textwrap.dedent("""
            ## ENC indicator — uiUtilities.c not found
            Add `#include "crypto/enc_indicator.h"` near the other includes,
            and call `enc_indicator_draw();` at the end of the header
            rendering function (`uiUtilityRenderHeader` in open-ham).
            """))
        return

    src = read(p)
    if 'enc_indicator_draw' in src:
        print("[uiUtilities.c] ENC indicator already wired"); return

    # 1) Add include
    try:
        insert_after(p,
            r'^#include\s+"user_interface/menuSystem\.h"',
            '#include "crypto/enc_indicator.h"',
            'uiUtilities.c include')
    except ModFailed:
        # Fall back to inserting after any user_interface/* include
        try:
            insert_after(p,
                r'^#include\s+"user_interface/[^"]+\.h"',
                '#include "crypto/enc_indicator.h"',
                'uiUtilities.c include (fallback)')
        except ModFailed:
            pass

    # 2) Insert the draw call near the end of uiUtilityRenderHeader
    src = read(p)
    m = re.search(r'\bvoid\s+uiUtilityRenderHeader\s*\([^)]*\)\s*\{',
                  src, re.MULTILINE)
    if not m:
        with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
            f.write(textwrap.dedent("""
            ## ENC indicator — function not found
            Locate the header-render function (likely `uiUtilityRenderHeader`)
            and add `enc_indicator_draw();` just before it returns.
            """))
        return

    # Find the closing brace at the same depth as the opening
    depth = 1
    i = m.end()
    while i < len(src) and depth > 0:
        if src[i] == '{': depth += 1
        elif src[i] == '}': depth -= 1
        i += 1
    if depth != 0:
        return
    insert_point = i - 1   # just before the closing brace
    payload = "\n\t/* AES patch: render ENC badge if encryption is configured */\n\tenc_indicator_draw();\n"
    if payload.strip() in src:
        print("[uiUtilities.c] ENC draw call already present"); return
    write(p, src[:insert_point] + payload + src[insert_point:])
    print(f"[uiUtilities.c] inserted enc_indicator_draw() call")

def mod_aes_header(root):
    """Configure the freshly-fetched tiny-AES-c aes.h for AES256 + CTR."""
    p = find(root, 'aes.h')
    if not p:
        raise ModFailed("aes.h not found (was tiny-AES-c fetched?)")
    s = read(p)
    for sym, val in (('AES128',0),('AES192',0),('AES256',1),
                     ('CBC',0),('CTR',1),('ECB',0)):
        s = re.sub(rf'^(#define\s+{sym}\s+)\d+',
                   rf'\g<1>{val}', s, flags=re.M)
        if not re.search(rf'^#define\s+{sym}\b', s, flags=re.M):
            s = s.replace('#ifndef _AES_H_',
                          f'#ifndef _AES_H_\n#define {sym} {val}', 1)
    write(p, s)
    print(f"[aes.h] configured: AES256=1, CTR=1")

def mod_build(root):
    """
    OpenGD77 builds with MCUXpresso (Eclipse). New files in firmware/source/crypto/
    are picked up by the IDE on Project > Refresh, but headless `make` builds
    need adding to .cproject or to a Makefile if present. Best-effort:
    """
    cproj = find(root, '.cproject')
    if cproj:
        with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
            f.write(textwrap.dedent("""
            ## Build system
            - In MCUXpresso: right-click the project, **Refresh**, then
              **Project > Clean** to rebuild.
            - The `firmware/source/crypto/` folder needs to be in the project's
              "Paths and Symbols > Source Location". MCUXpresso usually picks
              this up automatically; verify under Project Properties.
            - Include path: add `firmware/source` so `#include "crypto/aes.h"` resolves.
            """))
    mk = find(root, 'Makefile')
    if mk:
        # If a top-level Makefile exists, emit a hint
        with open(os.path.join(root, 'AES_PATCH_TODO.md'), 'a') as f:
            f.write(f"\n## Makefile detected at {mk} — add crypto/*.c to SRCS list.\n")

# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True, help='OpenGD77 source root')
    args = ap.parse_args()

    if not os.path.isdir(args.root):
        sys.exit(f"not a directory: {args.root}")

    # Empty TODO file at start
    todo = os.path.join(args.root, 'AES_PATCH_TODO.md')
    open(todo, 'w').write("# AES-256 patch — manual fixup required\n")

    fail = 0
    for fn in (mod_aes_header, mod_codeplug, mod_hrc6000_includes, mod_hrc6000_taps,
               mod_channel_details, mod_main_menu, mod_enc_indicator, mod_build):
        try: fn(args.root)
        except ModFailed as e:
            print("FAIL:", e); fail += 1

    print(f"\nDone. {fail} modifications failed; review AES_PATCH_TODO.md.")

if __name__ == '__main__':
    main()
