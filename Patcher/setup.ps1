<#
  setup.ps1 -- apply the AES-256 patch bundle in place to an existing
              OpenGD77 checkout. GitHub Desktop will handle commit + push.

  Requirements on host:
    - PowerShell 5.1+ (Windows 10/11 default) or PowerShell 7
    - Python 3      (https://python.org -- tick "Add to PATH" when installing)
    - Git           (Git for Windows, or GitHub Desktop's git on PATH)
    - curl.exe      (built into Windows 10+ -- no install needed)

  Usage examples:

    # from inside the OpenGD77 checkout:
    powershell -ExecutionPolicy Bypass -File C:\path\to\opengd77-aes-patch\setup.ps1

    # or from anywhere, pointing at the repo:
    powershell -ExecutionPolicy Bypass -File .\setup.ps1 -Repo C:\path\to\OpenGD77
#>

param(
    [string]$Repo = ""
)

$ErrorActionPreference = 'Stop'

trap {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Read-Host "Press Enter to close"
    exit 1
}

$TinyAesRaw = "https://raw.githubusercontent.com/kokke/tiny-AES-c/master"

function Test-IsRepo($p) {
    if (-not (Test-Path (Join-Path $p ".git") -PathType Container)) { return $false }
    foreach ($cand in @("firmware\source", "firmware\Source", "Source", "source")) {
        if (Test-Path (Join-Path $p $cand) -PathType Container) { return $true }
    }
    return $false
}

if ([string]::IsNullOrEmpty($Repo)) {
    if (Test-IsRepo (Get-Location).Path) {
        $Repo = (Get-Location).Path
    } else {
        throw "Need -Repo PATH (or run from inside an OpenGD77 checkout)."
    }
}

$Repo = (Resolve-Path $Repo).Path
if (-not (Test-Path (Join-Path $Repo ".git") -PathType Container)) {
    throw "$Repo is not a git checkout."
}

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

# Find Python on the system
$Python = $null
foreach ($n in @('python','python3')) {
    if (Get-Command $n -ErrorAction SilentlyContinue) { $Python = $n; break }
}
if (-not $Python -and (Get-Command 'py' -ErrorAction SilentlyContinue)) {
    $Python = 'py'
}
if (-not $Python) {
    throw "Python 3 not found. Install from https://python.org and tick 'Add to PATH'."
}

if (-not (Get-Command 'curl.exe' -ErrorAction SilentlyContinue)) {
    throw "curl.exe not found (should be built into Windows 10+)."
}
if (-not (Get-Command 'git' -ErrorAction SilentlyContinue)) {
    throw "git not found on PATH. Install Git for Windows or add GitHub Desktop's git to PATH."
}

# Locate firmware source root
$SrcRoot = $null
foreach ($cand in @("firmware\source","firmware\Source","Source","source")) {
    $full = Join-Path $Repo $cand
    if (Test-Path $full -PathType Container) { $SrcRoot = $full; break }
}
if (-not $SrcRoot) { throw "Could not find firmware source root in $Repo" }

Write-Host "==> repo:        $Repo"
Write-Host "==> source root: $SrcRoot"
Write-Host "==> python:      $Python"

# Fetch tiny-AES-c
Write-Host "==> fetching tiny-AES-c"
$cryptoDir = Join-Path $SrcRoot "crypto"
New-Item -ItemType Directory -Force -Path $cryptoDir | Out-Null

$aesC = Join-Path $cryptoDir "aes.c"
$aesH = Join-Path $cryptoDir "aes.h"

& curl.exe -fsSL "$TinyAesRaw/aes.c" -o $aesC
if ($LASTEXITCODE -ne 0) { throw "curl failed downloading aes.c" }
& curl.exe -fsSL "$TinyAesRaw/aes.h" -o $aesH
if ($LASTEXITCODE -ne 0) { throw "curl failed downloading aes.h" }

# Copy new sources
Write-Host "==> copying new crypto and UI sources"
Copy-Item -Path (Join-Path $Here "new_files\crypto\*") -Destination $cryptoDir -Force
$uiDir = Join-Path $SrcRoot "user_interface"
New-Item -ItemType Directory -Force -Path $uiDir | Out-Null
Copy-Item -Path (Join-Path $Here "new_files\user_interface\*") -Destination $uiDir -Force

# Apply anchored modifications (this also configures aes.h for AES256+CTR)
Write-Host "==> applying anchored modifications"
$applyMods = Join-Path $Here "apply_mods.py"

if ($Python -eq 'py') {
    & py -3 $applyMods --root $Repo
} else {
    & $Python $applyMods --root $Repo
}
if ($LASTEXITCODE -ne 0) { throw "apply_mods.py failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "Done. Working tree is modified -- review in GitHub Desktop before committing." -ForegroundColor Green
Write-Host "  Repo:   $Repo"
Write-Host "  Notes:  $(Join-Path $Repo 'AES_PATCH_TODO.md')"
Write-Host ""
Read-Host "Press Enter to close"
