#!/usr/bin/env bash
#
# setup.sh — apply the AES-256 patch bundle in place to an existing
#            OpenGD77 checkout. GitHub Desktop will handle commit + push.
#
# Requirements on host:
#   git, curl, python3
#
# Usage:
#   # from inside the OpenGD77 checkout:
#   /path/to/opengd77-aes-patch/setup.sh
#
#   # or from anywhere:
#   ./setup.sh --repo /path/to/OpenGD77
#
# After this finishes:
#   1. Open the repo in GitHub Desktop and review the changes.
#   2. Read AES_PATCH_TODO.md (in the repo root) and apply the small manual
#      fixups it lists.
#   3. Open in MCUXpresso, Refresh, build the UV380 configuration
#      (UV390 Plus is hardware-equivalent to UV380 for OpenGD77 purposes).

set -euo pipefail

REPO=""
TINY_AES_RAW="https://raw.githubusercontent.com/kokke/tiny-AES-c/master"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo) REPO="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1"; exit 1 ;;
    esac
done

# Default to current directory if it looks like an OpenGD77 checkout
if [[ -z "$REPO" ]]; then
    if [[ -d ".git" ]] && { [[ -d "firmware/source" ]] || [[ -d "firmware/Source" ]] || [[ -d "Source" ]] || [[ -d "source" ]]; }; then
        REPO="$(pwd)"
    else
        echo "error: --repo PATH required (or run from inside an OpenGD77 checkout)"
        exit 1
    fi
fi

REPO="$(cd "$REPO" && pwd)"
[[ -d "$REPO/.git" ]] || { echo "error: $REPO is not a git checkout"; exit 1; }

# Where this script lives (so we can find new_files/ and apply_mods.py)
HERE="$(cd "$(dirname "$0")" && pwd)"

for tool in git curl python3; do
    command -v "$tool" >/dev/null || { echo "missing: $tool"; exit 1; }
done

cd "$REPO"

# Locate firmware source root (varies a bit between forks)
SRC_ROOT=""
for cand in "firmware/source" "firmware/Source" "Source" "source"; do
    if [[ -d "$cand" ]]; then SRC_ROOT="$cand"; break; fi
done
[[ -z "$SRC_ROOT" ]] && { echo "could not find firmware source root in $REPO"; exit 1; }
echo "==> repo:        $REPO"
echo "==> source root: $SRC_ROOT"

# Drop in tiny-AES-c (configured for AES256 + CTR)
echo "==> fetching tiny-AES-c"
mkdir -p "$SRC_ROOT/crypto"
curl -fsSL "$TINY_AES_RAW/aes.c" -o "$SRC_ROOT/crypto/aes.c"
curl -fsSL "$TINY_AES_RAW/aes.h" -o "$SRC_ROOT/crypto/aes.h"

# Configure aes.h: AES256=1, CTR=1, others=0
python3 - <<'PY' "$SRC_ROOT/crypto/aes.h"
import re, sys
p = sys.argv[1]
s = open(p).read()
for sym, val in (('AES128',0),('AES192',0),('AES256',1),('CBC',0),('CTR',1),('ECB',0)):
    s = re.sub(rf'^(#define\s+{sym}\s+)\d+',  rf'\g<1>{val}', s, flags=re.M)
    if not re.search(rf'^#define\s+{sym}\b', s, flags=re.M):
        s = s.replace('#ifndef _AES_H_',
                      f'#ifndef _AES_H_\n#define {sym} {val}', 1)
open(p, 'w').write(s)
PY

# Drop in our new sources
echo "==> copying new crypto and UI sources"
cp -v "$HERE/new_files/crypto"/*.[ch]            "$SRC_ROOT/crypto/"
mkdir -p "$SRC_ROOT/user_interface"
cp -v "$HERE/new_files/user_interface"/*.c       "$SRC_ROOT/user_interface/"

# Anchored modifications to existing files
echo "==> applying anchored modifications"
python3 "$HERE/apply_mods.py" --root "$REPO"

echo
echo "Done. Working tree is modified — review in GitHub Desktop before committing."
echo "  Repo:   $REPO"
echo "  Notes:  $REPO/AES_PATCH_TODO.md"
