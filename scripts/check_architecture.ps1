#!/usr/bin/env pwsh
#Requires -Version 5.1
#
# VoxMic architecture guardrails.
# Ratcheting: every baseline may only go DOWN. Raising one requires an
# explicit decision recorded in the commit message.
#
# Baselines measured 2026-09-17 against the real tree.
#
# Usage:
#   powershell -File scripts/check_architecture.ps1
# Exit code 0 = PASS, 1 = FAIL.
#
# NOTE: this file must stay pure ASCII. Windows PowerShell 5.1 decodes
# BOM-less UTF-8 as ANSI (CP936) and swallows the newline that follows a
# non-ASCII byte, which silently merges the next line into a comment.

param(
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Src  = Join-Path $Root 'src'
$CMake = Join-Path $Root 'CMakeLists.txt'

# ---------------------------------------------------------------- baselines
# file path (relative to repo root) -> max allowed lines
#
# Two entries were raised for refactor step B2 (the two hang-point fixes): a
# bounded worker shutdown plus a diagnosable abandoned state does not fit the
# old budget.
#
# Payback status, rechecked after the Slint work was dropped:
#   main.cpp - PAID BACK. 638 lines, under the pre-B2 650, so the ceiling is
#     restored to 650 instead of being left at the 655 it was raised to.
#   dpdfnet_processor.cpp - PARTLY PAID BACK. 814 -> 791 -> 761 by extracting
#     two self-contained pieces to their own headers: the sherpa-onnx dynamic
#     loader (src/dsp/sherpa_onnx_api.h) and the block queue
#     (src/dsp/tagged_block_queue.h). Both are genuinely separate
#     responsibilities, not file-splitting for its own sake. The remaining ~33
#     over the pre-B2 728 is the abandoned-worker handling, accepted explicitly
#     here since nothing repays it now that B5 is cancelled.
$LineBaselines = @{
    'AGENTS.md'                     = 145   # loaded in full every session; keep it a rule sheet, not a manual
    'src/settings_dialog.cpp'       = 1458
    'src/main.cpp'                  = 650   # pre-B2 value; actual 650, at the line
    'src/mic_usage_monitor.cpp'     = 652
    'src/dsp/dpdfnet_processor.cpp' = 761   # pre-B2: 728; 33 lines of accepted debt
    'src/dsp/sherpa_onnx_api.h'     = 80    # extracted dynamic loader
    'src/dsp/tagged_block_queue.h'  = 70    # extracted SPSC block ring
}

# Measured 2026-09-17. Ratchet: these may only go DOWN. Raising one requires
# an explicit decision recorded in the commit message.
# Measured 0 after refactor step B3, which moved ~20 free globals into the
# single inline g_appState (src/app_state.h) and dropped the redundant `extern`
# keyword from the remaining function declarations. Ratchet: keep it at zero -
# shared state belongs in AppState, not in per-TU extern declarations.
$MaxExternTotal = 0       # extern decls in src/** minus src/dsp/rnnoise, minus extern "C"
$MaxU8Literals  = 0       # C++20 char8_t conflict - must stay zero
# 3 remaining sites: main.cpp:50 g_monitorThread, main.cpp:~622 bridge,
# wasapi_output.h:68 m_renderThread. The fourth - dpdfnet_processor.cpp's
# worker - became a std::jthread + stop_token in step B2, so this baseline
# ratcheted down from 4. The rest convert during B3.
$MaxBareThreads = 3       # std::thread without stop_token - new code must use jthread

$failures = New-Object System.Collections.ArrayList
$warnings = New-Object System.Collections.ArrayList

function Read-Lines([string]$Path) {
    # Always read raw: Get-Content in PS 5.1 mis-decodes UTF-8 and loses
    # newlines that follow non-ASCII bytes.
    return [System.IO.File]::ReadAllLines($Path)
}

function Add-Fail([string]$Msg) { [void]$failures.Add($Msg) }
function Add-Warn([string]$Msg) { [void]$warnings.Add($Msg) }

# ------------------------------------------------------- collect source set
# Vendored RNNoise is excluded: 27 files / ~277k lines, not ours to police.
$Sources = @()
if (Test-Path $Src) {
    $Sources = Get-ChildItem -Path $Src -Recurse -Include *.cpp,*.h -File |
        Where-Object { $_.FullName -notmatch 'rnnoise' }
}

# ------------------------------------------------- 1. file size ratchet
foreach ($rel in $LineBaselines.Keys) {
    $full = Join-Path $Root $rel
    if (-not (Test-Path $full)) {
        Add-Fail "missing file: $rel"
        continue
    }
    $count = (Read-Lines $full).Count
    $limit = $LineBaselines[$rel]
    if ($count -gt $limit) {
        Add-Fail "$rel = $count lines, exceeds baseline $limit (UI refactor should shrink this)"
    }
}

# ------------------------------------------------- 2. extern count ratchet
$externTotal = 0
foreach ($f in $Sources) {
    foreach ($line in (Read-Lines $f.FullName)) {
        # extern "C" is linkage, not shared mutable state - not what we police.
        if ($line -match '^\s*extern\s+"') { continue }
        if ($line -match '^\s*extern\s+') { $externTotal++ }
    }
}
if ($externTotal -gt $MaxExternTotal) {
    Add-Fail "extern declarations = $externTotal, exceeds baseline $MaxExternTotal"
}

# ------------------------------------------------- 3. C++23 redlines
$u8Count = 0
$bareThread = 0
foreach ($f in $Sources) {
    $lines = Read-Lines $f.FullName
    foreach ($line in $lines) {
        if ($line -match 'u8"') { $u8Count++ }
        if ($line -match 'std::thread\s') { $bareThread++ }
    }
}
if ($u8Count -gt $MaxU8Literals) {
    Add-Fail "u8 string literals = $u8Count (C++20 char8_t conflict); baseline $MaxU8Literals"
}
if ($bareThread -gt $MaxBareThreads) {
    Add-Fail "std::thread uses = $bareThread (new code must use std::jthread + stop_token); baseline $MaxBareThreads"
}

# ------------------------------------------------- 4. build redlines
if (Test-Path $CMake) {
    $cmakeText = (Read-Lines $CMake) -join "`n"
    if ($cmakeText -notmatch '/utf-8') {
        Add-Fail 'CMakeLists.txt missing /utf-8 (sherpa-onnx headers contain non-ASCII literals)'
    }
    if ($cmakeText -match 'target_precompile_headers' -and
        $cmakeText -match 'windows\.h' -and
        $cmakeText -notmatch 'NOMINMAX') {
        Add-Fail 'PCH includes <windows.h> but NOMINMAX is not defined -> std::min/std::max fail (C2589)'
    }
} else {
    Add-Fail 'CMakeLists.txt not found'
}

# ------------------------------------------------- 5. slint disclosure
# Royalty-free 2.0 license requires disclosing that Slint is used.
$slintFiles = @()
if (Test-Path (Join-Path $Root 'ui')) {
    $slintFiles = Get-ChildItem -Path (Join-Path $Root 'ui') -Recurse -Include *.slint -File
}
if ($slintFiles.Count -gt 0) {
    $hasDisclosure = $false
    foreach ($f in $slintFiles) {
        $text = (Read-Lines $f.FullName) -join "`n"
        if ($text -match 'AboutSlint') { $hasDisclosure = $true }
    }
    if (-not $hasDisclosure) {
        Add-Fail 'Slint files present but no AboutSlint widget -> Royalty-free license disclosure missing'
    }
}

# ------------------------------------------------- 6. single AGENTS.md
# A translated mirror drifted out of sync once; English is the only source.
$zhAgents = Join-Path $Root 'doc/zh-CN/AGENTS.md'
if (Test-Path $zhAgents) {
    Add-Fail 'doc/zh-CN/AGENTS.md exists - English AGENTS.md is the single source; do not re-create a translated mirror'
}

# ---------------------------------------------------------------- report
if (-not $Quiet) {
    Write-Output '=== VoxMic architecture guardrails ==='
    Write-Output "  files scanned     : $($Sources.Count)"
    Write-Output "  extern total      : $externTotal / $MaxExternTotal"
    Write-Output "  u8 literals       : $u8Count / $MaxU8Literals"
    Write-Output "  bare std::thread  : $bareThread / $MaxBareThreads"
    Write-Output ''
}

foreach ($w in $warnings) { Write-Output "  WARN  $w" }

if ($failures.Count -gt 0) {
    foreach ($f in $failures) { Write-Output "  FAIL  $f" }
    Write-Output ''
    Write-Output 'RESULT: FAIL'
    exit 1
}

Write-Output 'RESULT: PASS'
exit 0
