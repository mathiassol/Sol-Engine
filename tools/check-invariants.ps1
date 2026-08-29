#!/usr/bin/env pwsh
#
# Machine-checks the architectural invariants this engine states in prose.
#
# Every rule here is one an agent (or a tired human) would otherwise have to
# remember. Rules a script checks don't get violated; rules people remember do.
#
# Pure source analysis: no compiler, no GPU, no build directory. That is the
# point — this runs on a hosted CI runner, which `--gates` cannot, because the
# D3D12 backend skips software adapters and hosted runners have no hardware one.
#
#   pwsh tools/check-invariants.ps1            # report and exit 0/1
#   pwsh tools/check-invariants.ps1 -Quiet     # only failures
#
[CmdletBinding()]
param([switch]$Quiet)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo

$script:Results = @()

function Add-Result {
    param([string]$Name, [string]$Summary, [string[]]$Violations = @())
    $script:Results += [pscustomobject]@{
        Name       = $Name
        Summary    = $Summary
        Violations = $Violations
    }
}

# Every package's layer. Deliberately hardcoded: adding a package is an
# architectural decision, and an unlisted package fails the run rather than
# silently skipping the check.
#
# Ranks are finer than the four layers in ARCHITECTURE.md, because `math`
# legitimately sits on `core` while both are "Layer 0 - foundation". Rank is
# what the downward rule is checked against; the layer names are how the
# architecture is discussed.
$Layers = @{
    # Layer 0 - foundation
    'core' = 0
    'math' = 1
    # Layer 1 - interfaces
    'platform' = 2; 'rhi' = 2; 'shaders' = 2; 'assets' = 2; 'audio' = 2; 'physics' = 2
    # Layer 2 - implementations
    'platform-win32' = 3; 'rhi-d3d12' = 3; 'shaders-dxc' = 3; 'audio-xaudio2' = 3
    'physics-cpu' = 3; 'assets-filesystem' = 3; 'assets-obj' = 3; 'assets-gltf' = 3
    'assets-png-wic' = 3; 'assets-gpu' = 3
    # Layer 3 - systems
    'renderer' = 4; 'debug-draw' = 4; 'scene' = 4; 'gameplay' = 4
    # Layer 4 - runtime
    'engine' = 5
    # Tools and apps
    'cook' = 6; 'sandbox' = 6; 'game' = 6
}

$SourceGlobs = @('*.cpp', '*.hpp', '*.h', '*.hlsl', '*.hlsli')

function Get-PackageSources {
    param([string]$Package)
    $root = Join-Path 'packages' $Package
    if (-not (Test-Path $root)) { return @() }
    Get-ChildItem -Path $root -Recurse -File -Include $SourceGlobs -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' }
}

$packages = Get-ChildItem 'packages' -Directory | Select-Object -ExpandProperty Name | Sort-Object

# ── 1. Every package has a declared layer ────────────────────────────────────
$unlisted = $packages | Where-Object { -not $Layers.ContainsKey($_) }
$stale = $Layers.Keys | Where-Object { $_ -notin $packages }
Add-Result 'package-layers' "$($packages.Count) packages, all with a declared layer" `
    (@($unlisted | ForEach-Object { "packages/${_}: no layer declared - add it to `$Layers in this script" }) +
     @($stale | ForEach-Object { "$_ : listed in `$Layers but no such package" }))

# ── 2. Graphics-API isolation ────────────────────────────────────────────────
# The headline non-negotiable: the renderer never sees a graphics API header.
$apiRules = @(
    @{ Pattern = '#include\s*[<"](d3d12|dxgi|d3dcompiler|d3d12sdklayers)'; Allowed = @('rhi-d3d12'); What = 'D3D12/DXGI' }
    @{ Pattern = '#include\s*[<"]dxcapi';                                  Allowed = @('shaders-dxc'); What = 'DXC' }
    @{ Pattern = '#include\s*[<"](windows\.h|Windows\.h|wincodec|xaudio2|xinput|objbase|wrl/)'
       Allowed = @('platform-win32', 'rhi-d3d12', 'shaders-dxc', 'audio-xaudio2', 'assets-png-wic'); What = 'Win32' }
)
$apiViolations = @()
$scanned = 0
foreach ($pkg in $packages) {
    foreach ($file in Get-PackageSources $pkg) {
        $scanned++
        $text = Get-Content -LiteralPath $file.FullName -Raw
        foreach ($rule in $apiRules) {
            if ($pkg -in $rule.Allowed) { continue }
            $hits = Select-String -InputObject $text -Pattern $rule.Pattern -AllMatches
            if ($hits) {
                $rel = Resolve-Path -LiteralPath $file.FullName -Relative
                $apiViolations += "${rel}: $($rule.What) header in package '$pkg' (allowed only in: $($rule.Allowed -join ', '))"
            }
        }
    }
}
Add-Result 'graphics-api-isolation' "no API headers outside their backend ($scanned source files scanned)" $apiViolations

# ── 3. renderer never includes scene ─────────────────────────────────────────
$sceneViolations = @()
foreach ($file in Get-PackageSources 'renderer') {
    if (Select-String -LiteralPath $file.FullName -Pattern '#include\s*[<"]engine/scene/' -Quiet) {
        $rel = Resolve-Path -LiteralPath $file.FullName -Relative
        $sceneViolations += "${rel}: renderer includes scene - extract copies into a snapshot instead"
    }
}
Add-Result 'renderer-scene-isolation' 'renderer does not include scene' $sceneViolations

# ── 4. Dependencies only point downward ──────────────────────────────────────
$depViolations = @()
$edgeCount = 0
foreach ($pkg in $packages) {
    $cml = Join-Path (Join-Path 'packages' $pkg) 'CMakeLists.txt'
    if (-not (Test-Path $cml)) { continue }
    if (-not $Layers.ContainsKey($pkg)) { continue }
    $text = Get-Content -LiteralPath $cml -Raw
    foreach ($m in [regex]::Matches($text, 'engine::([a-z0-9\-]+)')) {
        $dep = $m.Groups[1].Value
        if ($dep -eq $pkg) { continue }
        $edgeCount++
        if (-not $Layers.ContainsKey($dep)) {
            $depViolations += "packages/$pkg/CMakeLists.txt: depends on unknown package '$dep'"
            continue
        }
        if ($Layers[$dep] -ge $Layers[$pkg]) {
            $depViolations += ("packages/$pkg/CMakeLists.txt: '$pkg' (layer $($Layers[$pkg])) depends on " +
                "'$dep' (layer $($Layers[$dep])) - dependencies must point strictly downward")
        }
    }
}
Add-Result 'dependency-direction' "$edgeCount package edges, all pointing downward" $depViolations

# ── 5. No empty / scaffolded packages ────────────────────────────────────────
# "Don't scaffold an empty package with no implementation."
$emptyViolations = @()
foreach ($pkg in $packages) {
    $cml = Join-Path (Join-Path 'packages' $pkg) 'CMakeLists.txt'
    if (-not (Test-Path $cml)) {
        $emptyViolations += "packages/${pkg}: no CMakeLists.txt"
        continue
    }
    $text = Get-Content -LiteralPath $cml -Raw
    $isInterface = $text -match '\bINTERFACE\b'
    # `game` builds from the sandbox sources via engine_add_runtime_app.
    $buildsElsewhere = $text -match 'engine_add_runtime_app'
    $hasSources = (Get-PackageSources $pkg).Count -gt 0
    if (-not $hasSources -and -not $isInterface -and -not $buildsElsewhere) {
        $emptyViolations += "packages/${pkg}: no sources, not an INTERFACE package - empty scaffold"
    }
}
Add-Result 'no-empty-packages' 'every package has an implementation' $emptyViolations

# ── 6. No engine render pass registered from an app ──────────────────────────
# Passes belong in renderer/src/standard_frame.cpp. The sandbox may only call
# add_pass on a local `probe` graph inside a render-graph validation gate.
$passViolations = @()
foreach ($app in @('sandbox', 'game')) {
    foreach ($file in Get-PackageSources $app) {
        foreach ($hit in Select-String -LiteralPath $file.FullName -Pattern 'add_pass') {
            if ($hit.Line -match 'probe\.add_pass') { continue }
            $rel = Resolve-Path -LiteralPath $file.FullName -Relative
            $passViolations += "${rel}:$($hit.LineNumber): add_pass outside renderer - engine passes go in standard_frame.cpp"
        }
    }
}
Add-Result 'no-app-render-passes' 'no engine pass registered from an app' $passViolations

# ── 7. Public headers live at include/engine/<domain>/ ───────────────────────
$headerViolations = @()
# Known, deliberate deviation: `engine` publishes engine.hpp and cvar_file.hpp
# at include/engine/ directly rather than include/engine/engine/. Fixing it
# would touch every consumer's include line, so it is recorded here rather than
# quietly passing. Do not add to this list without the same consideration.
$headerExceptions = @(
    'packages/engine/include/engine/engine.hpp',
    'packages/engine/include/engine/cvar_file.hpp'
)
foreach ($pkg in $packages) {
    $inc = Join-Path (Join-Path 'packages' $pkg) 'include'
    if (-not (Test-Path $inc)) { continue }
    Get-ChildItem -Path $inc -Recurse -File -Include '*.hpp', '*.h' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' } |
        ForEach-Object {
            $rel = ((Resolve-Path -LiteralPath $_.FullName -Relative) -replace '\\', '/') -replace '^\./', ''
            if ($rel -in $headerExceptions) { return }
            if ($rel -notmatch "packages/$([regex]::Escape($pkg))/include/engine/[^/]+/") {
                $headerViolations += "${rel}: public header not under include/engine/<domain>/"
            }
        }
}
Add-Result 'header-layout' 'public headers under include/engine/<domain>/' $headerViolations

# ── 8. Markdown links resolve ────────────────────────────────────────────────
# Catches the class of bug where a doc points at a file that was deleted or
# renamed.
#
# Two exclusions, both deliberate:
#   docs/superpowers/  - specs and plans are dated archives. A link that was
#                        valid when written is allowed to age.
#   reasarch/          - a personal research library. Its PDFs and figures are
#                        gitignored (paper extracts, public MIT repo), so its
#                        image embeds resolve on the author's disk and not in a
#                        clean checkout. Known and accepted, not silently.
$linkViolations = @()
$mdFiles = Get-ChildItem -Recurse -File -Include '*.md' |
    Where-Object {
        $_.FullName -notmatch '[\\/](build|node_modules|\.git)[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]superpowers[\\/]' -and
        $_.FullName -notmatch '[\\/]reasarch[\\/]'
    }
foreach ($md in $mdFiles) {
    $dir = Split-Path -Parent $md.FullName
    $lineNo = 0
    $inFence = $false
    foreach ($line in Get-Content -LiteralPath $md.FullName) {
        $lineNo++
        # A C++ lambda inside a fenced block - `[](const Foo& bar)` - is shaped
        # exactly like a markdown link. Skip fenced code entirely.
        if ($line -match '^\s*(```|~~~)') { $inFence = -not $inFence; continue }
        if ($inFence) { continue }
        foreach ($m in [regex]::Matches($line, '\[[^\]]*\]\(([^)]+)\)')) {
            $target = $m.Groups[1].Value
            if ($target -match '^(https?:|mailto:|#)') { continue }
            # A real link target has no whitespace; anything else is prose that
            # happens to match the shape.
            if ($target -match '\s') { continue }
            $path = ($target -split '#')[0]
            if ([string]::IsNullOrWhiteSpace($path)) { continue }
            if (-not (Test-Path -LiteralPath (Join-Path $dir $path))) {
                $rel = (Resolve-Path -LiteralPath $md.FullName -Relative) -replace '\\', '/'
                $linkViolations += "${rel}:${lineNo}: broken link -> $path"
            }
        }
    }
}
Add-Result 'doc-links' "$($mdFiles.Count) markdown files, all links resolve" $linkViolations

# ── 9. ROADMAP's LOC audit matches a recount ─────────────────────────────────
# These figures went stale twice before. Recount rather than trust.
$auditViolations = @()
$sourceFiles = Get-ChildItem -Path 'packages' -Recurse -File -Include '*.cpp', '*.hpp', '*.h', '*.hlsl', '*.hlsli' |
    Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' }
$actualFiles = $sourceFiles.Count
$actualLines = ($sourceFiles | ForEach-Object { (Get-Content -LiteralPath $_.FullName).Count } | Measure-Object -Sum).Sum
$roadmap = Get-Content -LiteralPath 'docs/ROADMAP.md' -Raw
$m = [regex]::Match($roadmap, '\*\*([\d,]+) lines\*\* of C\+\+/HLSL in \*\*([\d,]+) files\*\*,\s*\*\*(\d+)\s*\r?\n?packages\*\*')
if (-not $m.Success) {
    $auditViolations += 'docs/ROADMAP.md: could not find the "**N lines** of C++/HLSL in **M files**, **P packages**" audit line'
} else {
    $claimedLines = [int]($m.Groups[1].Value -replace ',', '')
    $claimedFiles = [int]($m.Groups[2].Value -replace ',', '')
    $claimedPkgs = [int]$m.Groups[3].Value
    if ($claimedLines -ne $actualLines) {
        $auditViolations += "docs/ROADMAP.md: audit claims $claimedLines lines, recount says $actualLines"
    }
    if ($claimedFiles -ne $actualFiles) {
        $auditViolations += "docs/ROADMAP.md: audit claims $claimedFiles files, recount says $actualFiles"
    }
    if ($claimedPkgs -ne $packages.Count) {
        $auditViolations += "docs/ROADMAP.md: audit claims $claimedPkgs packages, tree has $($packages.Count)"
    }
}
Add-Result 'roadmap-audit' "LOC audit matches recount ($actualLines lines, $actualFiles files)" $auditViolations

# ── 10. Design specs carry a resolved Status ─────────────────────────────────
# Four specs sat at "not yet implemented" long after shipping, because nothing
# revisited them.
$specViolations = @()
$specDir = 'docs/superpowers/specs'
if (Test-Path $specDir) {
    foreach ($spec in Get-ChildItem -Path $specDir -File -Filter '*.md') {
        $status = (Select-String -LiteralPath $spec.FullName -Pattern '^Status:\s*(.+)$' |
            Select-Object -First 1).Matches.Groups[1].Value
        if (-not $status) {
            $specViolations += "$specDir/$($spec.Name): no 'Status:' line"
        } elseif ($status.Trim() -notin @('implemented', 'spec', 'approved', 'rejected', 'superseded')) {
            $specViolations += "$specDir/$($spec.Name): unrecognised Status '$($status.Trim())' (use implemented / spec / approved / rejected / superseded)"
        }
    }
}
Add-Result 'spec-status' 'every design spec has a recognised Status' $specViolations

# ── report ───────────────────────────────────────────────────────────────────
Pop-Location

$failed = @($script:Results | Where-Object { $_.Violations.Count -gt 0 })

if (-not $Quiet) {
    Write-Host ''
    Write-Host 'Sol Engine - invariant checks' -ForegroundColor Cyan
    Write-Host ''
}

foreach ($r in $script:Results) {
    if ($r.Violations.Count -eq 0) {
        if (-not $Quiet) {
            Write-Host ('  [PASS] ' + $r.Name.PadRight(26) + $r.Summary) -ForegroundColor Green
        }
    } else {
        Write-Host ('  [FAIL] ' + $r.Name.PadRight(26) + "$($r.Violations.Count) violation(s)") -ForegroundColor Red
        foreach ($v in $r.Violations) {
            Write-Host "           $v" -ForegroundColor Yellow
        }
    }
}

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) of $($script:Results.Count) checks failed" -ForegroundColor Red
    exit 1
}
Write-Host "all $($script:Results.Count) checks passed" -ForegroundColor Green
exit 0
