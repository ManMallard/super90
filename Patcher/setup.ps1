<#
.SYNOPSIS
    Apply the Super90 / sneaky390 patch set to a fresh OpenGD77_MDUV380
    (STM32F4) source tree.

.PARAMETER Repo
    Path to the extracted OpenGD77_MDUV380 source. Should contain
    MDUV380_firmware/application/source/.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\setup.ps1 `
        -Repo C:\Users\me\OpenGD77_MDUV380_DM1701_20260130

.NOTES
    The Python patcher (apply_mods.py) does all file installation and
    drift detection. Output is written to PATCH_LOG.md inside -Repo.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Repo
)

$ErrorActionPreference = 'Stop'

function Fail($msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    Read-Host "Press Enter to close"
    exit 1
}

$Repo = (Resolve-Path $Repo).Path
$App  = Join-Path $Repo  'MDUV380_firmware\application'

if (-not (Test-Path $App)) {
    Fail "Not an OpenMDUV380 source tree (missing MDUV380_firmware\application). Repo=$Repo"
}

$Bundle = Split-Path $PSCommandPath -Parent
$Mods   = Join-Path $Bundle 'apply_mods.py'

if (-not (Test-Path $Mods)) { Fail "missing $Mods" }
if (-not (Test-Path (Join-Path $Bundle 'new_files')))         { Fail "missing $Bundle\new_files" }
if (-not (Test-Path (Join-Path $Bundle 'modified_files')))    { Fail "missing $Bundle\modified_files" }
if (-not (Test-Path (Join-Path $Bundle 'upstream_reference'))) { Fail "missing $Bundle\upstream_reference" }

# Step 1 — generate the AMBE codec placeholder.
# Without this, the assembler fails with "file not found: codec_bin_section_1.bin"
# because the OpenGD77 source ships without the proprietary AMBE codec.
$prep        = Join-Path $Repo 'prepare.bat'
$cleaner     = Join-Path $Repo 'MDUV380_firmware\tools\codec_cleaner.exe'
$placeholder = Join-Path $Repo 'MDUV380_firmware\application\source\linkerdata\codec_bin_section_1.bin'

if (-not (Test-Path $placeholder)) {
    Write-Host "Generating AMBE codec placeholder (prepare.bat)..." -ForegroundColor Cyan
    if (Test-Path $prep) {
        Push-Location $Repo
        & cmd /c prepare.bat
        $rc = $LASTEXITCODE
        Pop-Location
        if ($rc -ne 0) { Fail "prepare.bat returned $rc" }
    } elseif (Test-Path $cleaner) {
        Push-Location (Split-Path $placeholder -Parent)
        & $cleaner -C
        Pop-Location
    } else {
        Fail "neither prepare.bat nor codec_cleaner.exe found - check the source tree is the full official zip"
    }
    if (-not (Test-Path $placeholder)) {
        Fail "codec_bin_section_1.bin still missing after prepare step"
    }
    Write-Host "  codec placeholder created"
} else {
    Write-Host "AMBE codec placeholder already present, skipping prepare step" -ForegroundColor DarkGray
}

# Step 2 — locate Python.
$python = $null
foreach ($cmd in @('python','python3','py')) {
    $cand = Get-Command $cmd -ErrorAction SilentlyContinue
    if ($cand) { $python = $cand; break }
}
if (-not $python) { Fail "python not found on PATH (need Python 3.x)" }

# Step 3 — run the patcher.
Write-Host ""
Write-Host "Running Super90 / sneaky390 patcher..." -ForegroundColor Cyan
& $python $Mods $Repo
$patchRC = $LASTEXITCODE

Write-Host ""
$logPath = Join-Path $Repo 'PATCH_LOG.md'
switch ($patchRC) {
    0 {
        Write-Host "Patcher finished cleanly. All files applied." -ForegroundColor Green
    }
    3 {
        Write-Host "Patcher finished with items needing manual review." -ForegroundColor Yellow
        Write-Host "See: $logPath" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  - CONFLICT entries: upstream and sneaky390 both changed the same" -ForegroundColor Yellow
        Write-Host "                      lines. The file now contains <<<<<<<>>>>>>>" -ForegroundColor Yellow
        Write-Host "                      markers. Resolve them in your editor, then" -ForegroundColor Yellow
        Write-Host "                      re-run this script." -ForegroundColor Yellow
        Write-Host "  - MISSING / MANUAL: file expected by patcher not found, or other" -ForegroundColor Yellow
        Write-Host "                      condition the patcher refused to act on." -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  (OK_MERGED entries are NOT problems — they mean upstream changes" -ForegroundColor DarkGray
        Write-Host "   were merged in cleanly alongside our patches.)" -ForegroundColor DarkGray
        Write-Host ""
        $resp = Read-Host "Open PATCH_LOG.md now? (y/n)"
        if ($resp -match '^[yY]') { Start-Process notepad.exe -ArgumentList $logPath }
    }
    default {
        Fail "apply_mods.py returned exit code $patchRC"
    }
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Resolve any DRIFT / MISSING items listed in PATCH_LOG.md"
Write-Host "  2. Open the project in STM32CubeIDE"
Write-Host "  3. File -> Open Projects from File System -> select the MDUV380_firmware folder"
Write-Host "  4. Project -> Refresh, then Project -> Build"
Write-Host "  5. The CPS will splice in the real AMBE codec at flash time"
Write-Host ""
Read-Host "Press Enter to close"
