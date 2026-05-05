<#
.SYNOPSIS
    Apply the OpenGD77 AES-256 voice-encryption patch to an OpenMDUV380
    (STM32F4) source tree.

.PARAMETER Repo
    Path to the extracted OpenMDUV380 source.  Should contain
    MDUV380_firmware/application/source/.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\setup.ps1 `
        -Repo C:\sneaky390-uv380\OpenGD77_MDUV380_DM1701_20260130
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
$Src  = Join-Path $App   'source'
$Inc  = Join-Path $App   'include'

if (-not (Test-Path $App)) {
    Fail "Not an OpenMDUV380 source tree (missing MDUV380_firmware\application).  Repo=$Repo"
}

$Bundle = Split-Path $PSCommandPath -Parent
$NewSrc = Join-Path $Bundle 'new_files\source'
$NewInc = Join-Path $Bundle 'new_files\include'
$Mods   = Join-Path $Bundle 'apply_mods.py'

if (-not (Test-Path $NewSrc)) { Fail "missing $NewSrc" }
if (-not (Test-Path $NewInc)) { Fail "missing $NewInc" }
if (-not (Test-Path $Mods))   { Fail "missing $Mods" }

# 1. Run prepare.bat to generate the AMBE codec placeholder.
#    Without this, the assembler fails with "file not found:
#    codec_bin_section_1.bin" because the source ships without the
#    proprietary AMBE codec (it's spliced in by the CPS at flash time).
$prep = Join-Path $Repo 'prepare.bat'
$cleaner = Join-Path $Repo 'MDUV380_firmware\tools\codec_cleaner.exe'
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
        Fail "neither prepare.bat nor codec_cleaner.exe found - check that the source tree is the full official zip"
    }
    if (-not (Test-Path $placeholder)) {
        Fail "codec_bin_section_1.bin still missing after prepare step"
    }
    Write-Host "  codec placeholder created"
} else {
    Write-Host "AMBE codec placeholder already present, skipping prepare step" -ForegroundColor DarkGray
}

# 2. Copy new files into the tree
Write-Host "Copying new sources..." -ForegroundColor Cyan

# crypto subdirs
$null = New-Item -ItemType Directory -Force -Path (Join-Path $Src 'crypto')
$null = New-Item -ItemType Directory -Force -Path (Join-Path $Inc 'crypto')

Copy-Item -Path (Join-Path $NewSrc 'crypto\*') -Destination (Join-Path $Src 'crypto\') -Force
Copy-Item -Path (Join-Path $NewInc 'crypto\*') -Destination (Join-Path $Inc 'crypto\') -Force

# user_interface menu .c files
Copy-Item -Path (Join-Path $NewSrc 'user_interface\*') -Destination (Join-Path $Src 'user_interface\') -Force

Write-Host "  new_files copied"

# 3. Run the python patcher
Write-Host ""
Write-Host "Applying anchored modifications..." -ForegroundColor Cyan
$python = (Get-Command python -ErrorAction SilentlyContinue) `
        ?? (Get-Command python3 -ErrorAction SilentlyContinue) `
        ?? (Get-Command py -ErrorAction SilentlyContinue)
if (-not $python) { Fail "python not found on PATH" }

& $python $Mods $Repo
if ($LASTEXITCODE -ne 0) { Fail "apply_mods.py returned $LASTEXITCODE" }

# 4. Status
Write-Host ""
Write-Host "Done.  Working tree is modified." -ForegroundColor Green

$todo = Join-Path $Repo 'AES_PATCH_TODO.md'
if (Test-Path $todo) {
    Write-Host ""
    Write-Host "MANUAL EDITS REQUIRED — see $todo" -ForegroundColor Yellow
    Write-Host "Open it now? (y/n): " -ForegroundColor Yellow -NoNewline
    $resp = Read-Host
    if ($resp -match '^[yY]') { Start-Process notepad.exe -ArgumentList $todo }
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Open the project in STM32CubeIDE"
Write-Host "  2. File -> Open Projects from File System -> select the MDUV380_firmware folder"
Write-Host "  3. Project -> Refresh, then Project -> Build"
Write-Host "  4. The CPS will splice in the AMBE codec at flash time"
Write-Host ""
Read-Host "Press Enter to close"
