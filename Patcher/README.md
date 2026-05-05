# OpenGD77 AES-256 Patch Bundle

Adds AES-256-CTR voice encryption to OpenGD77 on the **MD-UV390 Plus**, with
a 16-slot key bank, T9 passphrase entry (PBKDF2-HMAC-SHA256 derivation), and
direct 64-char hex entry. Encryption is active only on digital (DMR) channels.

> **Bench testing only.** Encryption on amateur frequencies is prohibited in
> most jurisdictions. The format is OpenGD77-to-OpenGD77; not built for
> TYT-radio interop.

## Bundle contents

```
opengd77-aes-patch\
├── setup.ps1               # PowerShell — Windows / GitHub Desktop workflow
├── setup.sh                # bash equivalent (Git Bash, WSL, Linux, macOS)
├── apply_mods.py           # anchored insertions into existing files
├── new_files\
│   ├── crypto\             # sha256, kdf, key_storage, dmr_crypto
│   └── user_interface\     # menuKeyManagement, menuKeyEntry
└── README.md
```

`tiny-AES-c` is fetched at run-time from `kokke/tiny-AES-c` and configured
for `AES256=1, CTR=1`.

## Windows + GitHub Desktop usage

Prereqs:

- **Python 3** — install from python.org and tick "Add to PATH" during install.
- **Git** — bundled with Git for Windows or with GitHub Desktop. Verify it's
  on PATH by running `git --version` in PowerShell. If not, install
  Git for Windows.
- **curl.exe** — built into Windows 10 and 11. No install needed.

Run from PowerShell:

```powershell
# from inside your OpenGD77 checkout:
powershell -ExecutionPolicy Bypass -File C:\path\to\opengd77-aes-patch\setup.ps1

# or from anywhere, pointing at the repo:
powershell -ExecutionPolicy Bypass -File .\setup.ps1 -Repo C:\path\to\OpenGD77
```

The `-ExecutionPolicy Bypass` is needed because Windows blocks unsigned
PowerShell scripts by default. It only applies to that single invocation.

The script pauses with "Press Enter to close" on both success and error, so
the window won't vanish before you can read what happened.

After it finishes:

1. Open the OpenGD77 repo in **GitHub Desktop**. The diff view will show every
   change so you can review before committing.
2. Read `AES_PATCH_TODO.md` (created in the repo root). Apply the small
   manual fixups it lists — usually less than 5 lines each.
3. Commit and push from GitHub Desktop.
4. Open the project in MCUXpresso, **Project → Refresh**, build the
   UV380 configuration. (UV390 Plus is hardware-equivalent to UV380 from
   OpenGD77's perspective.)

> AMBE codec binaries are not needed at build time in your workflow — they
> get merged in by the OpenGD77 CPS during firmware upload to the radio.

## What's automated and what isn't

| Change | Automated | Notes |
|---|---|---|
| New crypto sources | yes | dropped into `firmware\source\crypto\` |
| New UI sources | yes | dropped into `firmware\source\user_interface\` |
| `aes.h` configured for AES256+CTR | yes | regex applied after download |
| `codeplug.h` — add `encKeyIndex` field | best-effort | finds a `LibreDMR_Pad`/reserved array and steals 1 byte |
| `HR-C6000.c` — encrypt/decrypt taps | best-effort | tries several anchor patterns; emits `AES_PATCH_TODO.md` if none match |
| `menuChannelDetails.c` — key-index selector | manual | TODO file gives exact code |
| `menuMainMenu.c` — top-level Encryption Keys entry | manual | TODO file gives exact code |
| MCUXpresso project file | manual | Refresh in IDE picks up new files |

## Architecture

Encryption sits at the latest TX point and earliest RX point you can hit in
software: the 27-byte AMBE+2 superframe payload exchanged with the HR-C6000
over SPI. The chip handles vocoder + FEC + interleave on its side, so by the
time bytes reach your code on TX they are pre-FEC ciphertext-ready, and by
the time they arrive on RX they have been deinterleaved and FEC-corrected
back to the cleartext byte stream.

```
TX:  PCM -> HR-C6000 AMBE encode -> [AES-CTR encrypt] -> HR-C6000 FEC -> RF
RX:  RF  -> HR-C6000 deint+FEC   -> [AES-CTR decrypt] -> HR-C6000 AMBE decode -> PCM
```

IV layout: 8-byte session nonce (per PTT, sent in unencrypted voice header LC) +
32-bit superframe number (free-running, both ends derive from chip frame
register) + 4 zero bytes.

## Things to verify before flying it

- **HR-C6000 anchor.** If `apply_mods.py` couldn't auto-place the taps,
  the TODO file shows exactly where to paste them. Buffer variable name is
  usually `audioAndHotspotDataBuffer` but check your tree.
- **Codeplug byte budget.** Confirm the byte stolen from `LibreDMR_Pad` (or
  whichever reserved array was found) doesn't collide with another patch
  you're carrying.
- **Power-on entropy.** `dmr_crypto_make_nonce()` mixes the millisecond
  tick with the chip UID — fine for bench testing, **not** strong enough
  for a real deployment.
- **CTR malleability.** Confidentiality only; no auth tag. If integrity
  matters, add a short HMAC-SHA256 tag in the LC and verify on RX.

## Reverting

In GitHub Desktop, **Repository → Discard All Changes…** before committing,
or after committing, **History →** right-click commit → **Revert**.
