#!/usr/bin/env pwsh
#
# Asserts that a packaged zip is actually self-contained, before anyone
# downloads it.
#
#   pwsh -NoProfile -File tools/check-shipped-zip.ps1 -Zip build/package/Sol-0.1.0-win64.zip
#
# Two things go wrong quietly. A build setting reverts and game.exe starts
# needing the Visual C++ redistributable again - it still runs on every
# developer machine, and fails only for the player who has never installed
# Visual Studio. Or an install() rule moves and dxcompiler.dll stops travelling,
# which fails at first shader compile rather than at launch.
#
# The redistributable test reads the exe's bytes for the import names rather
# than shelling out to dumpbin. A PE stores imported module names as plain
# ASCII in its import directory, so the string is present exactly when the
# import is - and this then needs no Visual Studio, which matters because the
# release workflow runs it on a bare runner.
#
# Exit 0 if the archive is shippable, 1 otherwise. Build #15.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Zip)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Zip)) { throw "no such archive: $Zip" }
$problems = @()

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("shipcheck-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    Expand-Archive -LiteralPath $Zip -DestinationPath $work
    Write-Host ("archive: {0} ({1:N2} MB)" -f (Split-Path -Leaf $Zip), ((Get-Item $Zip).Length / 1MB))

    # One top-level directory, so unzipping does not spill into the user's folder.
    $top = @(Get-ChildItem $work)
    if ($top.Count -ne 1 -or -not $top[0].PSIsContainer) {
        $problems += "archive has $($top.Count) top-level entries; expected one directory"
        $root = $work
    } else {
        $root = $top[0].FullName
        Write-Host "  top level: $($top[0].Name)/"
    }

    # Everything the runtime reaches for. content.pak and content/ are Build #3
    # and #5; the DXC pair is Build #9 and fails at first shader compile, not at
    # launch, which is the worse failure.
    foreach ($required in 'game.exe', 'dxcompiler.dll', 'dxil.dll', 'content.pak', 'LICENSE') {
        if (Test-Path -LiteralPath (Join-Path $root $required)) {
            Write-Host "  [ ok ] $required"
        } else {
            $problems += "missing from the archive: $required"
        }
    }
    foreach ($dir in 'content', 'debug') {
        if (Test-Path -LiteralPath (Join-Path $root $dir)) {
            $n = @(Get-ChildItem (Join-Path $root $dir) -Recurse -File).Count
            Write-Host "  [ ok ] $dir/ ($n files)"
        } else {
            $problems += "missing from the archive: $dir/"
        }
    }

    # Build leavings. *.obj is not here on purpose: cube.obj is a Wavefront mesh,
    # and .gitignore carves out the same exception for the same reason.
    $junk = @(Get-ChildItem $root -Recurse -File -Include '*.pdb', '*.ilk', '*.lib', '*.exp')
    foreach ($j in $junk) { $problems += "build leaving in the archive: $($j.Name)" }
    if ($junk.Count -eq 0) { Write-Host '  [ ok ] no .pdb / .lib / .ilk / .exp' }

    # The redistributable test.
    $exe = Join-Path $root 'game.exe'
    if (Test-Path -LiteralPath $exe) {
        $bytes = [System.IO.File]::ReadAllBytes($exe)
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
        $found = @()
        foreach ($name in 'VCRUNTIME140', 'MSVCP140', 'api-ms-win-crt') {
            if ($ascii.Contains($name)) { $found += $name }
        }
        if ($found.Count -gt 0) {
            $problems += ("game.exe references the Visual C++ redistributable (" +
                ($found -join ', ') + ") - a player without Visual Studio cannot run it. " +
                "CMAKE_MSVC_RUNTIME_LIBRARY in cmake/EngineDefaults.cmake should be MultiThreaded.")
        } else {
            Write-Host '  [ ok ] no redistributable import in game.exe'
        }
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($problems.Count -gt 0) {
    foreach ($p in $problems) { Write-Host "  FAIL $p" -ForegroundColor Red }
    Write-Host "$($problems.Count) problem(s) - not shippable" -ForegroundColor Red
    exit 1
}
Write-Host 'shippable' -ForegroundColor Green
exit 0
