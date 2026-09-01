# Proves that the root .clang-format is a real no-op, not a request.
#
# Visual Studio and the VS Code C/C++ extension both format by shelling out to
# clang-format.exe with the discovered config, so if the binary returns its
# input unchanged, the IDEs cannot rewrite this hand-tuned tree on save. That is
# the whole mechanism, and this script is its proof.
#
# Not part of the gate or the invariants: it needs clang-format, which is not a
# prerequisite of this project. `format-hygiene` covers the cheap half (the
# DisableFormat line is present) on every push; run this when you change the
# formatter setup or want the full byte-for-byte demonstration.
#
#     pwsh -NoProfile -File tools/probe-formatter.ps1
#
# Exit 0 = proven. Exit 1 = something formats when it should not. Exit 2 = no
# clang-format found, nothing proven either way.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo

# ── Find every clang-format on the machine. Prove it against all of them: the
#    one Visual Studio invokes is the one that matters, and a contributor may
#    have a different version on PATH.
$candidates = @()
$candidates += @(Get-Command clang-format -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
foreach ($pattern in @(
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-format.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-format.exe',
        'C:\Program Files\LLVM\bin\clang-format.exe',
        "$env:LOCALAPPDATA\Programs\LLVM\bin\clang-format.exe")) {
    $candidates += @(Get-ChildItem $pattern -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
}
$exes = @($candidates | Where-Object { $_ } | Sort-Object -Unique)

if ($exes.Count -eq 0) {
    Write-Host 'no clang-format found - nothing proven, and nothing broken.' -ForegroundColor Yellow
    Write-Host 'Visual Studio ships one at VC\Tools\Llvm\x64\bin\clang-format.exe.'
    exit 2
}

# ── The config must actually say so. Checked here as well as in the invariant,
#    because this script is what a person runs when they doubt the claim.
$root = Get-Content -LiteralPath (Join-Path $repo '.clang-format') -Raw
if ($root -notmatch '(?m)^\s*DisableFormat:\s*true\s*$') {
    Write-Host 'FAIL: .clang-format does not set DisableFormat: true' -ForegroundColor Red
    exit 1
}
Write-Host '.clang-format sets DisableFormat: true'

$sources = @(Get-ChildItem -Path (Join-Path $repo 'packages') -Recurse -File `
        -Include '*.cpp', '*.hpp', '*.h', '*.hlsl', '*.hlsli' |
    Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' })
Write-Host ("{0} source files, {1} clang-format binaries" -f $sources.Count, $exes.Count)

$failures = 0
foreach ($exe in $exes) {
    $version = (& $exe --version 2>&1 | Out-String).Trim()
    Write-Host ""
    Write-Host "── $version"
    Write-Host "   $exe"

    # ── CONTROL. A config parse error makes clang-format print nothing, and a
    #    naive reader scores silence as perfect conformance. So first prove the
    #    binary reformats something when told to, using an explicit style that
    #    does not depend on any file. If this control ever passes silently, every
    #    result below it is worthless.
    $probe = "int  main( ){int   x=1;return x;}"
    $ctl = ($probe | & $exe --assume-filename=probe.cpp --style=LLVM 2>&1 | Out-String)
    if ($ctl.Trim() -eq $probe) {
        Write-Host '   FAIL: control did not reformat - this binary is not working' -ForegroundColor Red
        $failures++
        continue
    }
    Write-Host '   control: reformats a dirty file when given an explicit style'

    # ── The claim: with the discovered config, output is byte-identical to input
    #    for every source file in the tree.
    $changed = @()
    foreach ($f in $sources) {
        $before = [System.IO.File]::ReadAllText($f.FullName)
        $after = (& $exe --style=file $f.FullName 2>&1 | Out-String)
        # clang-format writes LF; compare on normalised endings so a CRLF
        # working-tree file is not reported as a spurious difference.
        if (($before -replace "`r`n", "`n") -ne ($after -replace "`r`n", "`n")) {
            $changed += (Resolve-Path -Relative $f.FullName)
        }
    }
    if ($changed.Count -gt 0) {
        Write-Host ("   FAIL: {0} file(s) would be rewritten" -f $changed.Count) -ForegroundColor Red
        $changed | Select-Object -First 10 | ForEach-Object { Write-Host "     $_" }
        $failures++
    } else {
        Write-Host ("   no-op over all {0} source files" -f $sources.Count) -ForegroundColor Green
    }

    # ── And the other half: the house style must still work when asked for by
    #    name, or new files have no starting point.
    $styled = ($probe | & $exe --assume-filename=probe.cpp `
            --style=file:tools/house-style.clang-format 2>&1 | Out-String)
    if ($styled.Trim() -eq $probe) {
        Write-Host '   FAIL: tools/house-style.clang-format formatted nothing' -ForegroundColor Red
        $failures++
    } else {
        Write-Host '   tools/house-style.clang-format still formats a new file'
    }
}

Write-Host ""
if ($failures -gt 0) {
    Write-Host "$failures check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'formatter is disarmed, and the house style still works' -ForegroundColor Green
exit 0
