#!/usr/bin/env pwsh
#
# Names the prerequisite you are missing, instead of letting CMake fail with an
# error about itself.
#
# Two of them already fail well: a missing Windows SDK is a FATAL_ERROR naming
# DXC, and an over-long source path warns before project(). The rest do not.
# Without Visual Studio you get "No CMAKE_CXX_COMPILER could be found"; with
# CMake older than 4.2 you get "Could not create named generator Visual Studio
# 18 2026". Neither names the thing to install.
#
# Run this before your first configure:
#
#   pwsh -NoProfile -File tools/check-prereqs.ps1
#
# Exit 0 when everything the documented build needs is present, 1 otherwise.
# Warnings do not fail the run - they are things that work but will bite.
#
# Not an invariant, and it cannot be one: it inspects a machine, and CI runners
# have everything. See tools/check-invariants.ps1 for the checks that do run in
# CI. Build #16.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$script:Missing = 0
$script:Warned = 0

function Report {
    param(
        [string]$Name,
        [ValidateSet('ok', 'miss', 'warn')][string]$State,
        [string]$Detail,
        [string]$Fix = ''
    )
    $tag = '[ ok ]'
    $colour = 'Green'
    if ($State -eq 'miss') { $tag = '[MISS]'; $colour = 'Red'; $script:Missing++ }
    if ($State -eq 'warn') { $tag = '[warn]'; $colour = 'Yellow'; $script:Warned++ }
    Write-Host ("  $tag " + $Name.PadRight(22) + $Detail) -ForegroundColor $colour
    if ($Fix) { Write-Host ("         -> $Fix") -ForegroundColor DarkGray }
}

function Get-Exe([string]$Name) {
    $c = Get-Command $Name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    return $null
}

Write-Host ''
Write-Host 'Sol Engine - prerequisites' -ForegroundColor Cyan
Write-Host ''

# ── Operating system ─────────────────────────────────────────────────────────
# GPU_BASELINE.md is the authority on what a *player* needs; this is the build
# machine. Windows 10 1709 is the same floor because that is what the D3D12
# path targets.
$os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
if (-not $os) {
    Report 'Windows' 'warn' 'could not read the OS version'
} else {
    $build = [int]($os.BuildNumber)
    if ($build -ge 16299) {
        Report 'Windows' 'ok' "$($os.Caption.Trim()) (build $build)"
    } else {
        Report 'Windows' 'miss' "build $build is below 16299 (Windows 10 1709)" `
            'docs/GPU_BASELINE.md has the supported floor'
    }
}

# ── Shell ────────────────────────────────────────────────────────────────────
# Not a hard requirement - it is running right now - but which shell decides
# whether tools/check-invariants.ps1 runs without extra flags.
$psv = $PSVersionTable.PSVersion
if ($psv.Major -ge 7) {
    Report 'PowerShell' 'ok' "$psv (pwsh)"
} else {
    Report 'PowerShell' 'warn' "$psv (Windows PowerShell)" `
        ('its execution policy is Restricted by default, so scripts need ' +
         '-ExecutionPolicy Bypass. Installing PowerShell 7 sets RemoteSigned.')
}

# ── git ──────────────────────────────────────────────────────────────────────
$git = Get-Exe 'git'
if ($git) {
    Report 'git' 'ok' ((& git --version) -replace '^git version ', '')
} else {
    Report 'git' 'miss' 'not on PATH' 'https://git-scm.com/downloads'
}

# ── CMake ────────────────────────────────────────────────────────────────────
# Two floors, and they answer different questions: 3.24 is what this project's
# CMake code needs, 4.2 is where the documented "Visual Studio 18 2026"
# generator was added.
$cmake = Get-Exe 'cmake'
if (-not $cmake) {
    Report 'CMake' 'miss' 'not on PATH' `
        'https://cmake.org/download/ - or use the copy bundled with Visual Studio'
} else {
    $raw = (& cmake --version | Select-Object -First 1) -replace '^cmake version ', ''
    $ver = [version]($raw -replace '-.*$', '')
    if ($ver -ge [version]'4.2') {
        Report 'CMake' 'ok' $raw
    } elseif ($ver -ge [version]'3.24') {
        Report 'CMake' 'warn' "$raw - below 4.2" `
            ('the documented "Visual Studio 18 2026" generator needs 4.2. ' +
             'Another generator works on this version; the project floor is 3.24.')
    } else {
        Report 'CMake' 'miss' "$raw - below the project floor of 3.24" `
            'https://cmake.org/download/'
    }

    # A generator CMake *knows about*. Whether Visual Studio is installed is the
    # separate question below - CMake lists this whether or not it is.
    $gens = & cmake --help 2>&1
    if ($gens | Select-String -SimpleMatch 'Visual Studio 18 2026' -Quiet) {
        Report 'VS 18 generator' 'ok' 'available to this CMake'
    } else {
        Report 'VS 18 generator' 'miss' '"Visual Studio 18 2026" is not offered by this CMake' `
            'CMake 4.2 or newer, or configure with a different generator'
    }
}

# ── Visual Studio and the C++ workload ───────────────────────────────────────
# vswhere ships with every VS installer since 2017 and lives at a fixed path.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    Report 'Visual Studio' 'miss' 'no Visual Studio installer found' `
        'https://visualstudio.microsoft.com/ - workload "Desktop development with C++"'
} else {
    $vc = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    $name = & $vswhere -latest -products '*' -requires $vc -property displayName 2>$null
    $vsver = & $vswhere -latest -products '*' -requires $vc -property installationVersion 2>$null
    if ($name) {
        Report 'Visual Studio' 'ok' "$name ($vsver)"
    } else {
        $any = & $vswhere -latest -products '*' -property displayName 2>$null
        if ($any) {
            Report 'Visual Studio' 'miss' "$any has no C++ compiler" `
                'add the "Desktop development with C++" workload in the VS Installer'
        } else {
            Report 'Visual Studio' 'miss' 'installed, but no usable instance' `
                'https://visualstudio.microsoft.com/'
        }
    }
}

# ── Windows SDK DXC runtime ──────────────────────────────────────────────────
# Asked of the build's own search rather than a second copy of it: a duplicated
# search that drifts would report success on a machine where configure fails.
if ($cmake) {
    $script = Join-Path $PSScriptRoot 'report-dxc.cmake'
    # message() writes to stderr at every level, and Windows PowerShell 5.1 turns
    # a native command's stderr into an error record - which $ErrorActionPreference
    # = 'Stop' then throws on, for output that is not an error at all. pwsh 7 does
    # not. Relax it for this one call rather than for the whole script.
    $out = ''
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = (& cmake -P $script 2>&1 | Out-String) } finally { $ErrorActionPreference = $prev }
    $line = ($out -split "`r?`n" | Where-Object { $_ -match 'ENGINE_DXC_BIN_DIR=' } | Select-Object -First 1)
    $dir = ''
    if ($line) { $dir = ($line -replace '.*ENGINE_DXC_BIN_DIR=', '').Trim() }
    if ($dir) {
        Report 'Windows SDK DXC' 'ok' $dir
    } else {
        Report 'Windows SDK DXC' 'miss' 'dxcompiler.dll + dxil.dll not found' `
            ('install the Windows 10/11 SDK - it ships with the VS C++ workload. ' +
             'Configure fails with a FATAL_ERROR without it.')
    }
} else {
    Report 'Windows SDK DXC' 'warn' 'not checked - needs CMake'
}

# ── Source path length ───────────────────────────────────────────────────────
# The same 140 the CMakeLists guard uses. Duplicated deliberately and only here,
# because this script must work before CMake is ever run.
$len = $repo.Length
if ($len -le 140) {
    Report 'Source path' 'ok' "$len characters"
} else {
    Report 'Source path' 'miss' "$len characters, past the ~140 MAX_PATH allows" `
        'clone somewhere shorter, or enable LongPathsEnabled'
}

# ── Verdict ──────────────────────────────────────────────────────────────────
Write-Host ''
if ($script:Missing -gt 0) {
    Write-Host "$($script:Missing) prerequisite(s) missing - the documented build will not work yet." `
        -ForegroundColor Red
    exit 1
}
if ($script:Warned -gt 0) {
    Write-Host "ready to build, with $($script:Warned) note(s) above." -ForegroundColor Yellow
} else {
    Write-Host 'ready to build.' -ForegroundColor Green
}
Write-Host '  cmake --preset vs2026'
Write-Host '  cmake --build --preset debug'
Write-Host ''
exit 0
