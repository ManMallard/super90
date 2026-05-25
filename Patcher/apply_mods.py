#!/usr/bin/env python3
"""
apply_mods.py — applies the Super90 / sneaky390 patch set to a fresh
                OpenGD77_MDUV380 source tree using 3-way merge.

For each file we modify we bundle three things:

    Patcher/upstream_reference/<rel>   BASE   — the OpenGD77 file we forked from
    Patcher/modified_files/<rel>       OURS   — our patched version
    (the user's fresh tree file)       THEIRS — whatever the user has

At apply time we run `git merge-file` (standard git, no repo required):

    merged = merge(OURS, BASE, THEIRS)

So upstream changes the user inherited from a newer OpenGD77 release are
preserved automatically, *and* our patches are layered on top. The merge
only fails (conflict markers in the file) when upstream and we both
changed the same lines — in which case the user must decide.

Wholly-new files (M17 stack, crypto, key menus) live in Patcher/new_files
and are simply copied.

Statuses written to PATCH_LOG.md:
    NEW       installed a new file
    OK        applied cleanly (target == BASE, no upstream drift)
    OK_MERGED 3-way merged, upstream changes preserved alongside ours
    SKIP      target already matches our version, no work to do
    CONFLICT  3-way merge produced conflict markers — user must resolve
    MISSING   file expected in target tree not found
    MANUAL    patcher could not safely act (e.g. existing unresolved markers)

Exit code: 0 = clean, 3 = items need manual review, other = hard error.

Usage:
    python apply_mods.py <repo_root>
"""
from __future__ import annotations

import difflib
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NamedTuple

PATCHER_DIR = Path(__file__).resolve().parent
NEW_FILES_DIR = PATCHER_DIR / "new_files"
MODIFIED_FILES_DIR = PATCHER_DIR / "modified_files"
UPSTREAM_REF_DIR = PATCHER_DIR / "upstream_reference"

TARGET_APP_PREFIX = Path("MDUV380_firmware") / "application"


# ---------------------------------------------------------------------------
# Log entries
# ---------------------------------------------------------------------------
class LogEntry(NamedTuple):
    status: str
    rel_path: str
    detail: str
    conflict_block: str = ""   # shown verbatim in PATCH_LOG.md for CONFLICT entries
    upstream_diff: str = ""    # shown for OK_MERGED so the user can see what upstream added


log_entries: list[LogEntry] = []


def log(status: str, rel_path: str, detail: str,
        conflict_block: str = "", upstream_diff: str = "") -> None:
    entry = LogEntry(status, rel_path, detail, conflict_block, upstream_diff)
    log_entries.append(entry)
    tag = f"[{status}]".ljust(11)
    print(f"  {tag}{rel_path}  {detail}")


# ---------------------------------------------------------------------------
# Hashing & line-ending helpers
# ---------------------------------------------------------------------------
def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(p: Path) -> str:
    return sha256_bytes(p.read_bytes())


def detect_eol(data: bytes) -> bytes:
    """Return b'\\r\\n' if any CRLF found, else b'\\n'."""
    return b"\r\n" if b"\r\n" in data else b"\n"


def to_lf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


def apply_eol(data: bytes, eol: bytes) -> bytes:
    """Take LF-normalised data, write with the desired EOL."""
    if eol == b"\n":
        return data
    return data.replace(b"\n", b"\r\n")


def has_conflict_markers(data: bytes) -> bool:
    """Detect leftover conflict markers from a previous merge."""
    return (b"\n<<<<<<<" in data or data.startswith(b"<<<<<<<")) and b"\n>>>>>>>" in data


# ---------------------------------------------------------------------------
# git merge-file driver
# ---------------------------------------------------------------------------
class MergeResult(NamedTuple):
    merged: bytes        # LF-normalised merged content
    conflicts: int       # 0 = clean, >0 = number of conflict regions
    error: str = ""      # non-empty if git failed entirely


def merge_three_way(ours_lf: bytes, base_lf: bytes, theirs_lf: bytes,
                    rel_path: str) -> MergeResult:
    """Run `git merge-file` on three LF-normalised inputs. Returns merged
    content and conflict count."""
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        o = td_path / "ours"
        b = td_path / "base"
        t = td_path / "theirs"
        o.write_bytes(ours_lf)
        b.write_bytes(base_lf)
        t.write_bytes(theirs_lf)
        try:
            cp = subprocess.run(
                ["git", "merge-file",
                 "-L", f"OURS (sneaky390 patch for {rel_path})",
                 "-L", f"BASE (OpenGD77 reference)",
                 "-L", f"THEIRS (your fresh-tree {rel_path})",
                 str(o), str(b), str(t)],
                capture_output=True,
            )
        except FileNotFoundError:
            return MergeResult(b"", -1, "git binary not on PATH")
        if cp.returncode < 0:
            return MergeResult(b"", -1,
                               f"git merge-file failed: {cp.stderr.decode('utf-8', 'replace')}")
        return MergeResult(o.read_bytes(), cp.returncode)


def extract_conflict_blocks(merged: bytes) -> str:
    """Pull out the <<<<<<< ... >>>>>>> regions for the log."""
    text = merged.decode("utf-8", errors="replace")
    out: list[str] = []
    inside = False
    block: list[str] = []
    for line in text.splitlines():
        if line.startswith("<<<<<<<"):
            inside = True
            block = [line]
        elif inside:
            block.append(line)
            if line.startswith(">>>>>>>"):
                out.append("\n".join(block))
                block = []
                inside = False
    return ("\n\n---\n\n".join(out)).strip()


def upstream_diff_summary(base_lf: bytes, theirs_lf: bytes,
                          rel_path: str, max_lines: int = 200) -> str:
    """For OK_MERGED entries: short unified diff showing what upstream
    changed since we forked. Helps the user understand what the merge
    pulled in."""
    a = base_lf.decode("utf-8", errors="replace").splitlines(keepends=True)
    b = theirs_lf.decode("utf-8", errors="replace").splitlines(keepends=True)
    diff = list(difflib.unified_diff(
        a, b,
        f"BASE (OpenGD77 reference) {rel_path}",
        f"THEIRS (your fresh-tree)  {rel_path}",
        n=2,
    ))
    if not diff:
        return ""
    if len(diff) > max_lines:
        diff = diff[:max_lines] + [f"... [diff truncated; {len(diff) - max_lines} more lines] ...\n"]
    return "".join(diff)


# ---------------------------------------------------------------------------
# Phase 1: new files
# ---------------------------------------------------------------------------
def apply_new_files(repo: Path) -> None:
    print("\n[Phase 1] New files (no upstream version)")
    if not NEW_FILES_DIR.exists():
        log("MISSING", str(NEW_FILES_DIR.relative_to(PATCHER_DIR)),
            "new_files bundle directory not found")
        return
    for src in sorted(NEW_FILES_DIR.rglob("*")):
        if not src.is_file():
            continue
        rel = src.relative_to(NEW_FILES_DIR)
        dst = repo / TARGET_APP_PREFIX / rel
        rel_str = str(TARGET_APP_PREFIX / rel).replace("\\", "/")

        if dst.exists():
            if sha256_file(dst) == sha256_file(src):
                log("SKIP", rel_str, "already present and identical")
                continue
            backup = dst.with_suffix(dst.suffix + ".pre-patch")
            shutil.copy2(dst, backup)
            shutil.copy2(src, dst)
            log("MANUAL", rel_str,
                f"upstream added a same-named file; saved theirs as "
                f"{backup.name} and installed ours. Review by hand — "
                f"upstream may have implemented something we also did.")
            continue

        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        log("NEW", rel_str, "installed")


# ---------------------------------------------------------------------------
# Phase 2: modified files — 3-way merge
# ---------------------------------------------------------------------------
def apply_modified_file(ours_path: Path, base_path: Path, target_path: Path,
                        rel_str: str) -> None:
    target_bytes = target_path.read_bytes()
    ours_bytes = ours_path.read_bytes()
    base_bytes = base_path.read_bytes()

    # Refuse to touch a file with unresolved conflict markers from a
    # previous run — re-merging would garble it.
    if has_conflict_markers(target_bytes):
        log("MANUAL", rel_str,
            "target file contains unresolved <<<<<<<<>>>>>>> conflict markers "
            "from a previous patcher run. Resolve them first, then re-run.")
        return

    # Detect target's line-ending convention so we can write back with the same.
    target_eol = detect_eol(target_bytes)

    # Normalise all three to LF for diffing/merging.
    target_lf = to_lf(target_bytes)
    ours_lf = to_lf(ours_bytes)
    base_lf = to_lf(base_bytes)

    target_lf_hash = sha256_bytes(target_lf)
    ours_lf_hash = sha256_bytes(ours_lf)
    base_lf_hash = sha256_bytes(base_lf)

    # Already at our exact version (idempotent re-run).
    if target_lf_hash == ours_lf_hash:
        log("SKIP", rel_str, "already at sneaky390-patched version")
        return

    # Target is exactly the BASE we built against — clean apply.
    if target_lf_hash == base_lf_hash:
        target_path.write_bytes(apply_eol(ours_lf, target_eol))
        log("OK", rel_str, "applied (no upstream drift)")
        return

    # Otherwise: 3-way merge. This handles both "upstream improved this
    # file since we forked" AND "user has hand-edited it" — git merges
    # both sets of changes if they don't overlap.
    res = merge_three_way(ours_lf, base_lf, target_lf, rel_str)
    if res.error:
        log("MANUAL", rel_str, f"merge failed — {res.error}")
        return

    target_path.write_bytes(apply_eol(res.merged, target_eol))

    if res.conflicts == 0:
        diff = upstream_diff_summary(base_lf, target_lf, rel_str)
        log("OK_MERGED", rel_str,
            "3-way merge clean — upstream changes preserved + our patches applied",
            upstream_diff=diff)
        return

    conflict_text = extract_conflict_blocks(res.merged)
    log("CONFLICT", rel_str,
        f"{res.conflicts} conflict region(s) — upstream and sneaky390 both edited "
        f"the same lines. Resolve markers in the file, then re-run patcher.",
        conflict_block=conflict_text)


def apply_modified_files(repo: Path) -> None:
    print("\n[Phase 2] Modified files (3-way merge: upstream + sneaky390)")
    if not MODIFIED_FILES_DIR.exists() or not UPSTREAM_REF_DIR.exists():
        log("MISSING", "Patcher/modified_files or upstream_reference",
            "bundle directories not found — patcher cannot run")
        return

    for ours in sorted(MODIFIED_FILES_DIR.rglob("*")):
        if not ours.is_file():
            continue
        rel = ours.relative_to(MODIFIED_FILES_DIR)
        upstream = UPSTREAM_REF_DIR / rel
        target = repo / TARGET_APP_PREFIX / rel
        rel_str = str(TARGET_APP_PREFIX / rel).replace("\\", "/")

        if not upstream.exists():
            log("MANUAL", rel_str,
                "upstream_reference missing — cannot 3-way merge. "
                "Inspect Patcher/modified_files/<this file> and merge by hand.")
            continue

        if not target.exists():
            log("MISSING", rel_str,
                "file does not exist in target tree (upstream may have "
                "moved or renamed it). MANUAL REVIEW REQUIRED.")
            continue

        apply_modified_file(ours, upstream, target, rel_str)


# ---------------------------------------------------------------------------
# Phase 3: write PATCH_LOG.md
# ---------------------------------------------------------------------------
ATTENTION_STATUSES = ("CONFLICT", "MISSING", "MANUAL")


def write_log(repo: Path) -> int:
    path = repo / "PATCH_LOG.md"
    counts: dict[str, int] = {}
    for e in log_entries:
        counts[e.status] = counts.get(e.status, 0) + 1

    L: list[str] = []
    L.append("# Super90 / sneaky390 Patch Log\n\n")
    L.append("Generated by `Patcher/apply_mods.py`.\n\n")

    L.append("## Summary\n\n")
    for status in ("OK", "OK_MERGED", "NEW", "SKIP", "CONFLICT", "MISSING", "MANUAL"):
        if counts.get(status):
            L.append(f"- **{status}**: {counts[status]}\n")
    L.append("\n")
    L.append("Statuses:\n")
    L.append("- `OK` — target was identical to the OpenGD77 reference we built against. Our patches applied cleanly.\n")
    L.append("- `OK_MERGED` — upstream had changed this file since we forked. Both upstream's changes AND our patches were merged in cleanly (no overlap). See the per-file diff below to know exactly what upstream changed.\n")
    L.append("- `NEW` — wholly-new file installed (M17, crypto, etc.).\n")
    L.append("- `SKIP` — file was already at our patched version. No work needed.\n")
    L.append("- `CONFLICT` — upstream and sneaky390 both changed the same lines. The file now contains `<<<<<<< OURS / ======= / >>>>>>> THEIRS` markers. **Resolve them in your editor, then re-run the patcher.**\n")
    L.append("- `MISSING` — patcher expected this file but it was not in the target tree.\n")
    L.append("- `MANUAL` — patcher could not act safely. See detail.\n\n")

    attention = [e for e in log_entries if e.status in ATTENTION_STATUSES]
    if attention:
        L.append("## Items needing attention\n\n")
        for e in attention:
            L.append(f"### `{e.status}` — `{e.rel_path}`\n\n")
            L.append(f"{e.detail}\n\n")
            if e.conflict_block:
                L.append("Conflict region(s) embedded in the file:\n\n")
                L.append("```diff\n")
                L.append(e.conflict_block)
                L.append("\n```\n\n")

    merged = [e for e in log_entries if e.status == "OK_MERGED" and e.upstream_diff]
    if merged:
        L.append("## Upstream changes pulled in by 3-way merge\n\n")
        L.append("These files were updated by both upstream OpenGD77 and sneaky390 in non-overlapping areas. The merge succeeded cleanly. Showing what upstream changed so you can review:\n\n")
        for e in merged:
            L.append(f"### `{e.rel_path}`\n\n")
            L.append("```diff\n")
            L.append(e.upstream_diff)
            if not e.upstream_diff.endswith("\n"):
                L.append("\n")
            L.append("```\n\n")

    L.append("## Full action list\n\n")
    L.append("| Status | Path | Detail |\n")
    L.append("|--------|------|--------|\n")
    for e in log_entries:
        detail = e.detail.replace("|", "\\|").replace("\n", " ")
        L.append(f"| `{e.status}` | `{e.rel_path}` | {detail} |\n")

    path.write_text("".join(L), encoding="utf-8")
    print(f"\n  Log written to {path}")
    return sum(counts.get(s, 0) for s in ATTENTION_STATUSES)


# ---------------------------------------------------------------------------
def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: apply_mods.py <repo_root>", file=sys.stderr)
        return 2

    repo = Path(sys.argv[1]).resolve()
    app = repo / TARGET_APP_PREFIX
    if not app.exists():
        print(f"ERROR: not an OpenMDUV380 source tree (no {TARGET_APP_PREFIX} at {repo})",
              file=sys.stderr)
        return 1

    # Verify git is available — required for 3-way merge.
    try:
        subprocess.run(["git", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("ERROR: `git` not on PATH. The 3-way merger needs the git binary.\n"
              "       Install Git for Windows (or your distro's git package) and retry.",
              file=sys.stderr)
        return 1

    print(f"Applying Super90 / sneaky390 patch set (3-way merge)")
    print(f"  Target repo: {repo}")
    print(f"  Patcher dir: {PATCHER_DIR}")

    apply_new_files(repo)
    apply_modified_files(repo)
    needs_attention = write_log(repo)

    print()
    if needs_attention:
        print(f"  {needs_attention} item(s) need manual review — see PATCH_LOG.md")
        return 3
    print("  All files applied cleanly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
