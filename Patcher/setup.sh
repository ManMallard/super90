#!/usr/bin/env bash
#
# setup.sh — apply the Super90 / sneaky390 patch set to a fresh
#            OpenGD77_MDUV380 (STM32F4) source tree.
#
# Usage:
#   ./setup.sh <path-to-OpenGD77_MDUV380_tree>
#
# Output:
#   PATCH_LOG.md inside the target tree, listing every file action and any
#   DRIFT / MISSING / MANUAL items that need manual review.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <path-to-OpenGD77_MDUV380_tree>" >&2
    exit 2
fi

REPO="$(cd "$1" && pwd)"
APP="$REPO/MDUV380_firmware/application"
[[ -d "$APP" ]] || { echo "ERROR: not an OpenMDUV380 source tree (missing $APP)" >&2; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"
MODS="$HERE/apply_mods.py"

for d in new_files modified_files upstream_reference; do
    [[ -d "$HERE/$d" ]] || { echo "ERROR: missing $HERE/$d" >&2; exit 1; }
done
[[ -f "$MODS" ]] || { echo "ERROR: missing $MODS" >&2; exit 1; }

# Step 1 — AMBE codec placeholder
PLACEHOLDER="$REPO/MDUV380_firmware/application/source/linkerdata/codec_bin_section_1.bin"
if [[ ! -f "$PLACEHOLDER" ]]; then
    echo "==> generating AMBE codec placeholder"
    if [[ -f "$REPO/prepare.bat" ]]; then
        # WSL / Cygwin path
        ( cd "$REPO" && cmd.exe /c prepare.bat ) || true
    fi
    if [[ ! -f "$PLACEHOLDER" ]] && [[ -x "$REPO/MDUV380_firmware/tools/codec_cleaner" ]]; then
        ( cd "$(dirname "$PLACEHOLDER")" && "$REPO/MDUV380_firmware/tools/codec_cleaner" -C )
    fi
    if [[ ! -f "$PLACEHOLDER" ]]; then
        echo "ERROR: could not generate codec_bin_section_1.bin — run prepare.bat manually first" >&2
        exit 1
    fi
fi

# Step 2 — pick a python
PY=""
for cand in python3 python py; do
    if command -v "$cand" >/dev/null 2>&1; then PY="$cand"; break; fi
done
[[ -n "$PY" ]] || { echo "ERROR: no python found on PATH" >&2; exit 1; }

# Step 3 — run patcher
echo
echo "==> running Super90 / sneaky390 patcher"
set +e
"$PY" "$MODS" "$REPO"
RC=$?
set -e

echo
LOG="$REPO/PATCH_LOG.md"
case "$RC" in
    0) echo "Patcher finished cleanly. All files applied." ;;
    3) echo "Patcher finished with items needing manual review. See: $LOG" ;;
    *) echo "ERROR: apply_mods.py returned $RC" >&2; exit "$RC" ;;
esac

echo
echo "Next steps:"
echo "  1. Resolve any DRIFT / MISSING items listed in $LOG"
echo "  2. Open the project in STM32CubeIDE and build"
echo "  3. The CPS splices in the real AMBE codec at flash time"
