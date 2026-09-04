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
    'reflect' = 1
    # Layer 1 - interfaces
    'platform' = 2; 'rhi' = 2; 'shaders' = 2; 'assets' = 2; 'audio' = 2; 'physics' = 2
    # Layer 2 - implementations
    'platform-win32' = 3; 'rhi-d3d12' = 3; 'rhi-vulkan' = 3; 'shaders-dxc' = 3
    'audio-xaudio2' = 3
    'physics-cpu' = 3; 'assets-filesystem' = 3; 'assets-obj' = 3; 'assets-gltf' = 3
    'assets-png-wic' = 3; 'assets-gpu' = 3
    # Layer 3 - systems
    'renderer' = 4; 'debug-draw' = 4; 'scene' = 4; 'gameplay' = 4
    # Layer 4 - runtime
    'engine' = 5; 'scene-render' = 5
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
    # Vulkan is as much a graphics API as D3D12, so it gets the same fence.
    # Two backends means this rule protects the renderer from both
    # directions rather than only from the one that happened to be first.
    @{ Pattern = '#include\s*[<"](vulkan/|volk\.h)'
       Allowed = @('rhi-vulkan'); What = 'Vulkan' }
    @{ Pattern = '#include\s*[<"](windows\.h|Windows\.h|wincodec|xaudio2|xinput|objbase|wrl/)'
       Allowed = @('platform-win32', 'rhi-d3d12', 'rhi-vulkan', 'shaders-dxc',
                    'audio-xaudio2', 'assets-png-wic'); What = 'Win32' }
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
# Three exclusions, all deliberate:
#   docs/superpowers/  - specs and plans are dated archives. A link that was
#                        valid when written is allowed to age.
#   docs/analysis/     - generated audit reports from /aim-audit. Same reason
#                        as superpowers/: dated snapshots, and an older report
#                        may reference a file that has since moved. A
#                        generated file must never be able to turn CI red.
#   reasarch/          - a personal research library. Its PDFs and figures are
#                        gitignored (paper extracts, public MIT repo), so its
#                        image embeds resolve on the author's disk and not in a
#                        clean checkout. Known and accepted, not silently.
$linkViolations = @()
$mdFiles = Get-ChildItem -Recurse -File -Include '*.md' |
    Where-Object {
        $_.FullName -notmatch '[\\/](build|node_modules|\.git)[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]superpowers[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]analysis[\\/]' -and
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

# ── 11. ENGINE_MAP dependency graph is sound ─────────────────────────────────
# The backlog is only usable if every Later row has a path to Ready. Two
# failures make that untrue and neither is visible by reading:
#
#   * a dependency loop - two rows each naming the other, so neither can ever
#     become Ready. There were two of these, both created by prose that
#     mentioned a row in order to say it was *not* a blocker.
#   * a stale blocker - a Later row whose named rows are all Done, which should
#     have been flipped to Ready when the last one shipped.
#
# The map's own rule is what makes this checkable: a `Category #N` reference in
# Finish first *is* a blocker, and nothing else in the column is. Walls are
# written without a reference.
$mapViolations = @()
$mapPath = 'docs/ENGINE_MAP.md'
$mapSummary = 'ENGINE_MAP.md not present'
if (Test-Path $mapPath) {
    $catNames = @{}
    $rowStatus = @{}
    $rowFirst  = @{}
    $catClaim = @{}       # category -> @(done, ready, rows) as the map claims
    $catTally = @{}       # category -> @(done, ready, rows) as recounted here
    $headerClaim = $null  # the file's own whole-backlog totals
    $totals = @{ Done = 0; Ready = 0; Later = 0; Far = 0 }
    $curCat = 0
    foreach ($line in Get-Content -LiteralPath $mapPath) {
        $h = [regex]::Match($line, '^##\s+(\d+)\.\s+(.*)$')
        if ($h.Success) {
            $curCat = [int]$h.Groups[1].Value
            $catNames[$curCat] = $h.Groups[2].Value.Trim()
            continue
        }
        # The map states its own arithmetic twice: once at the top for the whole
        # backlog, once per category. Separators are matched as \D+ rather than
        # a literal middle dot, so this does not depend on how the file decodes.
        if ($null -eq $headerClaim) {
            $ht = [regex]::Match($line,
                '^(\d+) Done\D+(\d+) Ready\D+(\d+) Later\D+(\d+) Far')
            if ($ht.Success) {
                $headerClaim = @([int]$ht.Groups[1].Value, [int]$ht.Groups[2].Value,
                    [int]$ht.Groups[3].Value, [int]$ht.Groups[4].Value)
            }
        }
        if ($curCat -eq 0) { continue }
        $st = [regex]::Match($line, '^\*(\d+) done\D+(\d+) ready\D+(\d+) rows\.\*$')
        if ($st.Success) {
            $catClaim[$curCat] = @([int]$st.Groups[1].Value, [int]$st.Groups[2].Value,
                [int]$st.Groups[3].Value)
            continue
        }
        $r = [regex]::Match($line, '^\|\s*(\d+)\s*\|(.*?)\|(.*?)\|(.*?)\|\s*$')
        if (-not $r.Success) { continue }
        $key = "$curCat/$([int]$r.Groups[1].Value)"
        $status = ($r.Groups[3].Value -replace '\*', '').Trim()
        # The # column is an id, not a position, so a repeat is a bookkeeping
        # error - and a silent one: $rowStatus is keyed by it, so the second row
        # used to overwrite the first and both the row count and the ready count
        # quietly lost one. Found exactly that way, by adding a second #15.
        if ($rowStatus.ContainsKey($key)) {
            $mapViolations += "ENGINE_MAP $key : duplicate row id - ids are unique per category"
        }
        $rowStatus[$key] = $status
        $rowFirst[$key]  = $r.Groups[4].Value.Trim()
        if (-not $catTally.ContainsKey($curCat)) { $catTally[$curCat] = @(0, 0, 0) }
        $catTally[$curCat][2] += 1
        if ($totals.ContainsKey($status)) { $totals[$status] += 1 }
        if ($status -eq 'Done')  { $catTally[$curCat][0] += 1 }
        if ($status -eq 'Ready') { $catTally[$curCat][1] += 1 }
    }

    # First word of each category heading is how rows refer to it.
    $alias = @{}
    foreach ($n in $catNames.Keys) {
        $first = ($catNames[$n] -split '[ /(,]')[0].ToLower()
        $alias[$first] = $n
    }
    $alias['ui'] = 12   # heading is "In-game UI"

    $deps = @{}
    foreach ($key in $rowStatus.Keys) {
        $list = @()
        foreach ($m in [regex]::Matches($rowFirst[$key], '\b([A-Z][A-Za-z]*)\s*#(\d+)')) {
            $w = $m.Groups[1].Value.ToLower()
            if (-not $alias.ContainsKey($w)) {
                $mapViolations += "ENGINE_MAP $key : Finish first names unknown category '$($m.Groups[1].Value)'"
                continue
            }
            $t = "$($alias[$w])/$([int]$m.Groups[2].Value)"
            if (-not $rowStatus.ContainsKey($t)) {
                $mapViolations += "ENGINE_MAP $key : dangling reference $($m.Groups[1].Value) #$($m.Groups[2].Value)"
                continue
            }
            $list += $t
        }
        $deps[$key] = $list

        if ($rowStatus[$key] -in @('Done', 'Ready') -and $rowFirst[$key]) {
            $mapViolations += "ENGINE_MAP $key : status $($rowStatus[$key]) but Finish first is filled"
        }
        if ($rowStatus[$key] -eq 'Later' -and -not $rowFirst[$key]) {
            $mapViolations += "ENGINE_MAP $key : Later with an empty Finish first - name a row or a wall"
        }
    }

    foreach ($key in $deps.Keys) {
        if ($rowStatus[$key] -ne 'Later' -or $deps[$key].Count -eq 0) { continue }
        $undone = @($deps[$key] | Where-Object { $rowStatus[$_] -ne 'Done' })
        if ($undone.Count -eq 0) {
            $mapViolations += "ENGINE_MAP $key : every named blocker is Done - this row should be Ready"
        }
    }

    # Cycles, over not-Done rows only: a Done blocker cannot hold anything up.
    $adj = @{}
    foreach ($key in $deps.Keys) {
        $adj[$key] = @($deps[$key] | Where-Object { $rowStatus[$_] -ne 'Done' })
    }
    $colour = @{}
    foreach ($key in $adj.Keys) { $colour[$key] = 'white' }
    $found = @()
    function Find-MapCycle {
        param([string]$Node, [System.Collections.ArrayList]$Stack)
        $script:colour[$Node] = 'grey'
        [void]$Stack.Add($Node)
        foreach ($next in $script:adj[$Node]) {
            $c = if ($script:colour.ContainsKey($next)) { $script:colour[$next] } else { 'black' }
            if ($c -eq 'grey') {
                $at = $Stack.IndexOf($next)
                $script:found += , @($Stack[$at..($Stack.Count - 1)] + $next)
            } elseif ($c -eq 'white') {
                Find-MapCycle -Node $next -Stack $Stack
            }
        }
        $Stack.RemoveAt($Stack.Count - 1)
        $script:colour[$Node] = 'black'
    }
    $script:adj = $adj; $script:colour = $colour; $script:found = $found
    foreach ($key in @($adj.Keys)) {
        if ($script:colour[$key] -eq 'white') {
            Find-MapCycle -Node $key -Stack ([System.Collections.ArrayList]::new())
        }
    }
    foreach ($cyc in $script:found) {
        $names = @()
        foreach ($node in $cyc) {
            $parts = $node -split '/'
            $catWord = ($catNames[[int]$parts[0]] -split '[ /(,]')[0]
            $names += ($catWord + ' #' + $parts[1])
        }
        $pretty = $names -join ' -> '
        $mapViolations += ('ENGINE_MAP dependency loop: ' + $pretty)
    }

    # Reconcile the map's own arithmetic against the tables under it. This is
    # what roadmap-audit does for ROADMAP's LOC figures; these 22 numbers - one
    # header line plus one subtotal per category - had no equivalent, and a
    # number nothing checks is a number that drifts.
    if ($null -eq $headerClaim) {
        $mapViolations += 'ENGINE_MAP: no "N Done / N Ready / N Later / N Far" totals line found'
    } else {
        $order = @('Done', 'Ready', 'Later', 'Far')
        for ($i = 0; $i -lt $order.Count; $i++) {
            $claimed = $headerClaim[$i]
            $counted = $totals[$order[$i]]
            if ($claimed -ne $counted) {
                $mapViolations += ("ENGINE_MAP totals: header claims $claimed " +
                    "$($order[$i]), tables have $counted")
            }
        }
    }
    foreach ($n in ($catNames.Keys | Sort-Object)) {
        $tally = if ($catTally.ContainsKey($n)) { $catTally[$n] } else { @(0, 0, 0) }
        if (-not $catClaim.ContainsKey($n)) {
            $mapViolations += ("ENGINE_MAP category $n ($($catNames[$n])): no " +
                "'*N done / N ready / N rows.*' subtotal line")
            continue
        }
        $labels = @('done', 'ready', 'rows')
        for ($i = 0; $i -lt $labels.Count; $i++) {
            if ($catClaim[$n][$i] -ne $tally[$i]) {
                $mapViolations += ("ENGINE_MAP category $n ($($catNames[$n])): " +
                    "subtotal claims $($catClaim[$n][$i]) $($labels[$i]), " +
                    "tables have $($tally[$i])")
            }
        }
    }

    $ready = @($rowStatus.Values | Where-Object { $_ -eq 'Ready' }).Count
    $mapSummary = ("$($rowStatus.Count) rows, $ready ready, totals and " +
        "$($catClaim.Count) subtotals agree, no dependency loops")
}
Add-Result 'map-dependencies' $mapSummary $mapViolations
# ── 12. Conditionally-added packages are only ever linked conditionally ──────
# `target_link_libraries(app PRIVATE engine::foo)` where foo's add_subdirectory
# sits inside an `if()` is a CMake **generate-time** error wherever that
# condition is false - not a warning, and not something a build ever reaches.
#
# Nothing else here catches it. `--gates` needs a GPU. `dependency-direction`
# below reads layer ranks, not conditions. And CI's configure-options matrix
# runs on windows-latest, where `if(WIN32)` is always true - so the one job that
# exists to keep the modularity claim honest cannot see this class at all.
#
# Found by the 31 Aug audit: engine_add_runtime_app linked engine::assets-png-wic
# unconditionally while the package is added only under if(WIN32), so
# `cmake -B build` failed at configure on Linux and macOS - step one of this
# project's stated cross-platform goal, broken with no signal anywhere.
function Get-CMakeSubdirConditions {
    # packages/<X> -> the if() conditions enclosing its add_subdirectory.
    # An empty list means unconditional.
    param([string]$Path)
    $map = @{}
    $stack = New-Object System.Collections.Generic.List[string]
    foreach ($line in Get-Content -LiteralPath $Path) {
        $t = $line.Trim()
        if ($t -match '^if\s*\((.*)\)\s*$') { $stack.Add($Matches[1]); continue }
        if ($t -match '^endif\s*\(') {
            if ($stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1) }
            continue
        }
        if ($t -match '^add_subdirectory\(packages/([a-z0-9\-]+)\)') {
            $map[$Matches[1]] = @($stack)
        }
    }
    return $map
}

$subdirConditions = Get-CMakeSubdirConditions 'CMakeLists.txt'
$conditionalPkgs = @($subdirConditions.Keys | Where-Object { $subdirConditions[$_].Count -gt 0 })

$condLinkViolations = @()
$condLinkCount = 0
$cmakeFiles = @(Get-ChildItem 'cmake' -File -Filter '*.cmake' -ErrorAction SilentlyContinue) +
              @(Get-ChildItem 'packages' -Recurse -File -Filter 'CMakeLists.txt' -ErrorAction SilentlyContinue)
foreach ($file in $cmakeFiles) {
    # A package that is itself conditional may reference its siblings freely:
    # packages/cook is inside if(WIN32), so its link to engine::assets-png-wic
    # can never be evaluated on a platform where that target is absent.
    $owner = $null
    if ($file.FullName -match '[\\/]packages[\\/]([a-z0-9\-]+)[\\/]CMakeLists\.txt$') {
        $owner = $Matches[1]
    }
    if ($owner -and $subdirConditions.ContainsKey($owner) -and
        $subdirConditions[$owner].Count -gt 0) { continue }

    $stack = New-Object System.Collections.Generic.List[string]
    $lineNo = 0
    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $lineNo++
        $t = $line.Trim()
        # The guard itself lives on the `if` line, so it is never a reference.
        if ($t -match '^if\s*\((.*)\)\s*$') { $stack.Add($Matches[1]); continue }
        if ($t -match '^endif\s*\(') {
            if ($stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1) }
            continue
        }
        $guarded = @($stack | Where-Object { $_ -match '\bTARGET\b|\bWIN32\b' }).Count -gt 0
        foreach ($m in [regex]::Matches($t, 'engine::([a-z0-9\-]+)')) {
            $dep = $m.Groups[1].Value
            if ($dep -notin $conditionalPkgs) { continue }
            $condLinkCount++
            if ($guarded) { continue }
            $rel = ((Resolve-Path -LiteralPath $file.FullName -Relative) -replace '\\', '/') -replace '^\./', ''
            $condLinkViolations += ("${rel}:${lineNo}: references engine::$dep unguarded, but " +
                "packages/$dep is added only under if($($subdirConditions[$dep] -join ' && ')) - " +
                "wrap it in if(TARGET engine::$dep), or configure fails wherever that is false")
        }
    }
}
Add-Result 'conditional-target-links' `
    "$condLinkCount references to conditional packages, all guarded" $condLinkViolations

# ── 13. The tree obeys the .editorconfig it ships ────────────────────────────
# .editorconfig is this project's formatting contract and nothing enforced it
# until now. The root .clang-format cannot: it is `DisableFormat: true`, a
# deliberate no-op, because Visual Studio applies a discovered .clang-format as
# you type and this tree is hand-formatted (92 of 123 C++ files diverge from the
# closest config anyone could write for it, measured 1 Sep 2026). So the rules
# that ARE universally true here get checked instead — and the no-op itself gets
# checked, so it cannot quietly stop being one.
#
# Trailing whitespace is checked for sources but not for markdown, where two
# trailing spaces are a hard line break and .editorconfig exempts it.
# docs/analysis/ is skipped for the same reason doc-links skips it: it is
# generated output, and a generated file must never be able to turn CI red.
$fmtViolations = @()
$fmtColumnLimit = 100

function Add-FmtViolation {
    param([string]$Path, [int]$Line, [string]$Message)
    $rel = ((Resolve-Path -LiteralPath $Path -Relative) -replace '\\', '/') -replace '^\./', ''
    if ($Line -gt 0) {
        $script:fmtViolations += "${rel}:${Line}: $Message"
    } else {
        $script:fmtViolations += "${rel}: $Message"
    }
}

# Read once as bytes: a BOM, a missing final newline and a stray CRLF are all
# invisible to Get-Content, which is exactly why they rot unnoticed.
function Test-FmtFile {
    param([string]$Path, [bool]$CheckColumns, [bool]$CheckTrailing)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) { return }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        Add-FmtViolation $Path 1 'UTF-8 BOM - .editorconfig sets charset = utf-8, which means no BOM'
    }
    if ($bytes[$bytes.Length - 1] -ne 0x0A) {
        Add-FmtViolation $Path 0 'no final newline - .editorconfig sets insert_final_newline = true'
    }
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($text.Contains("`r`n")) {
        Add-FmtViolation $Path 0 'CRLF line endings - .editorconfig sets end_of_line = lf here'
    }
    $lineNo = 0
    foreach ($raw in ($text -split "`n")) {
        $lineNo++
        $line = $raw.TrimEnd([char]13)
        if ($line.Contains([char]9)) {
            Add-FmtViolation $Path $lineNo 'tab character - .editorconfig sets indent_style = space'
        }
        if ($CheckColumns -and $line.Length -gt $fmtColumnLimit) {
            Add-FmtViolation $Path $lineNo `
                "$($line.Length) columns - .editorconfig sets max_line_length = $fmtColumnLimit"
        }
        if ($CheckTrailing -and $line.Length -gt 0 -and $line -ne $line.TrimEnd()) {
            Add-FmtViolation $Path $lineNo `
                'trailing whitespace - .editorconfig sets trim_trailing_whitespace = true'
        }
    }
}

$fmtSources = @(Get-ChildItem -Path 'packages' -Recurse -File -Include $SourceGlobs -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' })
foreach ($f in $fmtSources) { Test-FmtFile $f.FullName $true $true }

$fmtMarkdown = @(Get-ChildItem -Recurse -File -Include '*.md' |
    Where-Object {
        $_.FullName -notmatch '[\\/](build|node_modules|\.git)[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]analysis[\\/]' -and
        $_.FullName -notmatch '[\\/]reasarch[\\/]' -and
        $_.FullName -notmatch '[\\/]third_party[\\/]'
    })
foreach ($f in $fmtMarkdown) { Test-FmtFile $f.FullName $false $false }

# The mirror image: .gitattributes pins these to CRLF because cmd.exe mis-parses
# a multi-line if/for block in an LF-only .bat. A bare LF here is the defect.
$fmtShell = @(Get-ChildItem -Recurse -File -Include '*.ps1', '*.bat', '*.cmd' |
    Where-Object { $_.FullName -notmatch '[\\/](build|node_modules|\.git)[\\/]' })
foreach ($f in $fmtShell) {
    $text = [System.IO.File]::ReadAllText($f.FullName)
    $bare = ([regex]::Matches($text, "(?<!`r)`n")).Count
    if ($bare -gt 0) {
        Add-FmtViolation $f.FullName 0 `
            "$bare line(s) end LF - .gitattributes and .editorconfig pin CRLF for shell scripts"
    }
}

# The formatter must stay disarmed. Losing this one line re-arms Visual Studio's
# format-as-you-type over a tree that no clang-format config describes.
if (-not (Test-Path -LiteralPath '.clang-format')) {
    $fmtViolations += '.clang-format: missing - it must exist and be a no-op, or editors reformat on save'
} elseif ((Get-Content -LiteralPath '.clang-format' -Raw) -notmatch '(?m)^\s*DisableFormat:\s*true\s*$') {
    $fmtViolations += ('.clang-format: DisableFormat: true is missing - Visual Studio applies a ' +
        'discovered .clang-format as you type, so without it one save rewrites the tree. ' +
        'Prove it either way with tools/probe-formatter.ps1')
}
if (-not (Test-Path -LiteralPath 'tools/house-style.clang-format')) {
    $fmtViolations += ('tools/house-style.clang-format: missing - .clang-format and ' +
        '.claude/rules/cpp-conventions.md both send new files there')
}

Add-Result 'format-hygiene' `
    ("$($fmtSources.Count) sources, $($fmtMarkdown.Count) markdown, $($fmtShell.Count) shell " +
     'match .editorconfig; .clang-format still a no-op') $fmtViolations

# ── 14. Every gate is declared and classified ────────────────────────────────
# A gate that exists but is in no sequence runs nowhere and says nothing - the
# same silent-absence failure the FramePipelines static_assert was added to stop
# (analizeMax A1), one layer up and in the one place a static_assert cannot
# reach. Three things have to agree: the definitions in gates/gates_*.cpp, the
# declarations in gates/gates.hpp, and the kGates table in gate_registry.cpp
# that classifies each one Cpu or Gpu.
#
# Source-only, so it runs in CI with no compiler and no GPU.
$gateViolations = @()
$gateDir = 'packages/sandbox/src/gates'
$gateSummary = 'gates/ not present'

if (Test-Path $gateDir) {
    $defined = @{}
    foreach ($f in Get-ChildItem $gateDir -File -Filter 'gates_*.cpp') {
        $lineNo = 0
        foreach ($line in Get-Content -LiteralPath $f.FullName) {
            $lineNo++
            if ($line -match '^(?:\[\[maybe_unused\]\] )?bool (run_\w+_gate)\(') {
                $name = $Matches[1]
                if ($defined.ContainsKey($name)) {
                    $gateViolations += "$($f.Name):${lineNo}: $name is defined twice"
                }
                $defined[$name] = "$($f.Name):$lineNo"
            }
        }
    }

    $header = Get-Content -LiteralPath (Join-Path $gateDir 'gates.hpp') -Raw
    $declared = @{}
    foreach ($m in [regex]::Matches($header, 'bool (run_\w+_gate)\(')) {
        $declared[$m.Groups[1].Value] = $true
    }

    $registry = Get-Content -LiteralPath (Join-Path $gateDir 'gate_registry.cpp') -Raw
    $classified = @{}
    foreach ($m in [regex]::Matches($registry, '\{"(run_\w+_gate)",\s*GateKind::(Cpu|Gpu)')) {
        $n = $m.Groups[1].Value
        if ($classified.ContainsKey($n)) {
            $gateViolations += "gate_registry.cpp: $n appears in kGates twice"
        }
        $classified[$n] = $m.Groups[2].Value
    }

    foreach ($name in $defined.Keys) {
        if (-not $declared.ContainsKey($name)) {
            $gateViolations += ("$($defined[$name]): $name is defined but not declared in " +
                "gates.hpp - main.cpp cannot call it")
        }
        if (-not $classified.ContainsKey($name)) {
            $gateViolations += ("$($defined[$name]): $name is not in kGates - add it to " +
                "gate_registry.cpp as Cpu or Gpu, or it runs in no sequence")
        }
    }
    foreach ($name in $declared.Keys) {
        if (-not $defined.ContainsKey($name)) {
            $gateViolations += "gates.hpp: $name is declared but no gates_*.cpp defines it"
        }
    }
    foreach ($name in $classified.Keys) {
        if (-not $defined.ContainsKey($name)) {
            $gateViolations += "gate_registry.cpp: kGates names $name, which no gates_*.cpp defines"
        }
    }

    # A Cpu entry must carry a function; a Gpu entry must not. The header says
    # the same thing and run_cpu_gates asserts it, but at runtime and only on the
    # entries a headless run reaches.
    foreach ($m in [regex]::Matches($registry,
            '\{"(run_\w+_gate)",\s*GateKind::Gpu,\s*([^\}]*?)\}')) {
        if ($m.Groups[2].Value.Trim() -ne 'nullptr') {
            $gateViolations += "gate_registry.cpp: $($m.Groups[1].Value) is Gpu but carries a function"
        }
    }
    foreach ($m in [regex]::Matches($registry, '\{"(run_\w+_gate)",\s*GateKind::Cpu,\s*nullptr')) {
        $gateViolations += "gate_registry.cpp: $($m.Groups[1].Value) is Cpu but carries nullptr"
    }

    $cpu = @($classified.Values | Where-Object { $_ -eq 'Cpu' }).Count
    $gateSummary = "$($defined.Count) gates, all declared and classified ($cpu Cpu, $($defined.Count - $cpu) Gpu)"
}

Add-Result 'gate-registry' $gateSummary $gateViolations

# ── 15. The RHI interface speaks no backend's vocabulary ─────────────────────
# graphics-api-isolation already stops a d3d12.h from being *included* outside
# its backend. This stops the vocabulary leaking instead of the header, which is
# what actually happened: GraphicsPipelineDesc's counts were named after D3D12
# root parameters and a comment read "Root SRVs in register space 1 ... (t0..tN,
# space1)". Register spaces are HLSL, and a Vulkan backend reading that header
# learns the wrong model (analizeMax A2).
#
# Since rhi-vulkan the banned list covers Vulkan vocabulary too. A header
# kept neutral only away from D3D12 would drift toward whichever backend was
# written second, and the check would then be enforcing a preference instead
# of neutrality.
#
# Two allowances, both deliberate. rhi.hpp's GraphicsAPI enumerator names the
# backends because that is what it is for. The binding contract in
# resources.hpp names both on purpose - it is the translation table, and a
# table that cannot say "D3D12" or "descriptor set" is useless.
$vocabViolations = @()
$vocabSummary = 'packages/rhi/include not present'
$vocabRoot = 'packages/rhi/include'

if (Test-Path $vocabRoot) {
    # Word-boundary anchored, so `describe` does not match `SRV` and
    # `unordered_map` does not match `UAV`.
    $banned = @(
        @{ Pattern = '\bD3D12\b'; Why = 'names a backend' },
        @{ Pattern = '\bDXGI\b'; Why = 'names a backend' },
        @{ Pattern = '\bSRV\b|\bUAV\b|\bCBV\b|\bRTV\b|\bDSV\b'
           Why = 'is a D3D descriptor kind' },
        @{ Pattern = 'root signature|root parameter|descriptor table'
           Why = 'is a D3D12 layout concept' },
        @{ Pattern = 'register space'; Why = 'is HLSL register syntax' },
        @{ Pattern = '\b[tbsu][0-9]+\.\.'
           Why = 'is HLSL register syntax' },
        # Symmetry, added with rhi-vulkan. A header kept neutral only away from
        # D3D12 would drift toward whichever backend was written second, and the
        # check would then be enforcing a preference rather than neutrality.
        @{ Pattern = '\bVk[A-Z]\w+'; Why = 'is a Vulkan type' },
        @{ Pattern = '\bVK_[A-Z]'; Why = 'is a Vulkan enumerant or macro' },
        @{ Pattern = '\bSPIR-V\b|\bSPIRV\b'
           Why = 'is a Vulkan bytecode format' },
        @{ Pattern = 'descriptor set|pipeline layout|push constant'
           Why = 'is a Vulkan layout concept' }
    )
    $scanned = 0
    foreach ($f in Get-ChildItem $vocabRoot -Recurse -File -Include '*.hpp', '*.h') {
        $scanned++
        $rel = ((Resolve-Path -LiteralPath $f.FullName -Relative) -replace '\\', '/') -replace '^\./', ''
        $inContract = $false
        $lineNo = 0
        foreach ($line in Get-Content -LiteralPath $f.FullName) {
            $lineNo++
            # The contract block runs from its banner to the first line that is
            # not a comment, so it cannot silently grow to cover real code.
            if ($line -match 'The binding contract') { $inContract = $true }
            elseif ($inContract -and $line.Trim() -notmatch '^//') { $inContract = $false }
            if ($inContract) { continue }
            # rhi.hpp's Backend enumerator.
            if ($rel.EndsWith('rhi.hpp') -and $line.Trim() -match '^(D3D12|Vulkan|Metal|None),?$') {
                continue
            }
            foreach ($b in $banned) {
                if ($line -match $b.Pattern) {
                    $vocabViolations += ("${rel}:${lineNo}: '" + $Matches[0] + "' " + $b.Why +
                        " - the RHI interface is backend-agnostic. Say it in neutral terms, or " +
                        "put the per-backend detail in resources.hpp's binding contract.")
                    break
                }
            }
        }
    }
    $vocabSummary = "$scanned public RHI headers, no backend vocabulary outside the binding contract"
}

Add-Result 'rhi-vocabulary' $vocabSummary $vocabViolations

# ── 16. every default-constructed ShaderCompileDesc names its target ─────────
# ShaderCompileDesc::target defaults to Dxil, so a desc that never sets it asks
# for the D3D backend's bytecode wherever it is used. On a Vulkan device the
# pipeline then rejects the blob, the setup function returns early, and every
# gate after it in that function silently does not run - fifty of them, twice,
# with a green pass count both times. Nothing fails; the gates are just absent,
# which the pass count alone does not show.
#
# Two offenders existed when this was written, run_rhi_impl_gate and
# run_color_space_gate, and both were found by hand. That is the argument for
# checking it mechanically.
#
# A desc copy-initialised from another one is exempt: it inherits the target,
# and that is the idiom the sandbox uses for its vertex/pixel pairs.
$targetViolations = @()
$targetScanned = 0
$descFiles = Get-ChildItem -Path 'packages' -Recurse -File -Include '*.cpp' |
    Where-Object { $_.FullName -notmatch '[\\/]third_party[\\/]' }
foreach ($file in $descFiles) {
    $lines = Get-Content -LiteralPath $file.FullName
    $rel = ($file.FullName.Substring($repo.Length + 1)) -replace '\\', '/'
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # `ShaderCompileDesc name{};` only - a copy (`= other;`) inherits.
        if ($lines[$i] -notmatch 'ShaderCompileDesc\s+(\w+)\s*\{\s*\}\s*;') { continue }
        $name = $Matches[1]
        $targetScanned++
        # The assignment sits in the run of field writes that follows.
        $found = $false
        $limit = [Math]::Min($i + 14, $lines.Count - 1)
        for ($j = $i + 1; $j -le $limit; $j++) {
            if ($lines[$j] -match ('^\s*' + [regex]::Escape($name) + '\.target\s*=')) {
                $found = $true
                break
            }
        }
        if (-not $found) {
            $targetViolations += ("${rel}:" + ($i + 1) + ": '$name' never sets .target, so " +
                'it asks for DXIL wherever it is used - which a Vulkan device rejects at ' +
                'pipeline creation. Set it from the device: shader_target_for(device).')
        }
    }
}
Add-Result 'shader-target' `
    "$targetScanned default-constructed ShaderCompileDesc, all naming a target" `
    $targetViolations


# ── 17. Skill frontmatter says what it looks like it says ─────────────────
# In YAML a space-then-hash starts a comment, so an unquoted value carrying a
# row reference is silently cut at it. `description: ... Invoke as /aim-row
# renderer #16.` parsed as `... Invoke as /aim-row renderer` - losing the worked
# example and, in two skills, the entire "not for this, use that instead"
# clause. Those two fields are what the model reads to decide whether a skill
# applies, so the truncation changes behaviour while the file still looks right.
# It went unnoticed until the harness printed a skill listing back.
#
# Also checks name matches its directory, because a skill is invoked by
# directory name and a mismatched `name:` is addressable by neither reliably.
$skillViolations = @()
$skillScanned = 0
$skillFiles = Get-ChildItem -Path '.claude/skills' -Recurse -File -Filter 'SKILL.md' -EA 0
foreach ($file in $skillFiles) {
    $lines = Get-Content -LiteralPath $file.FullName
    $rel = ($file.FullName.Substring($repo.Length + 1)) -replace '\\', '/'
    if ($lines.Count -lt 2 -or $lines[0].Trim() -ne '---') {
        $skillViolations += "${rel}:1: no YAML frontmatter - the first line must be '---'."
        continue
    }
    # The frontmatter is everything up to the next bare '---'.
    $end = -1
    for ($i = 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -eq '---') { $end = $i; break }
    }
    if ($end -lt 0) {
        $skillViolations += "${rel}:1: frontmatter is never closed by a '---'."
        continue
    }
    $skillScanned++
    $dir = Split-Path -Leaf (Split-Path -Parent $file.FullName)
    $sawName = $false
    for ($i = 1; $i -lt $end; $i++) {
        $line = $lines[$i]
        if ($line -match '^name:\s*(.+?)\s*$') {
            $sawName = $true
            $declared = $Matches[1].Trim([char]39, [char]34)
            if ($declared -ne $dir) {
                $skillViolations += ("${rel}:" + ($i + 1) + ": name '$declared' does not match " +
                    "its directory '$dir' - a skill is invoked by directory name.")
            }
        }
        if ($line -match '^(description|when_to_use):\s*(.*)$') {
            $field = $Matches[1]
            $value = $Matches[2]
            $quoted = $value.StartsWith([char]39) -or $value.StartsWith([char]34)
            if ((-not $quoted) -and $value -match '\s#') {
                $skillViolations += ("${rel}:" + ($i + 1) + ": $field is unquoted and contains ' #', " +
                    'so YAML cuts it there and the rest is lost. Wrap the value in single quotes.')
            }
        }
    }
    if (-not $sawName) { $skillViolations += "${rel}:1: frontmatter has no 'name:'." }
}
Add-Result 'skill-frontmatter' `
    "$skillScanned skills, frontmatter intact and named after its directory" `
    $skillViolations


# ── 18. Docs state no fact the tree contradicts ──────────────────────────────
# The drift this catches is always the same shape: one fact lives in several
# files, something changes, and only some of the copies are updated. `rhi-vulkan`
# shipped, passed the whole gate suite and rendered a live frame while
# ARCHITECTURE.md's package table, its interface/implementation table and both
# CMake-option tables still described one GPU backend - and one of them told the
# reader not to build the package that already existed.
#
# So this checks the three lists that are *derivable from the tree* and were
# each wrong at once. It deliberately checks lists and names, never prose: a
# count in a sentence is what `roadmap-audit` does for LOC, and the fix for the
# rest is to have one owner per fact rather than to verify every copy.
$claimViolations = @()

$archPath = 'docs/ARCHITECTURE.md'
$readmePath = 'README.md'
$arch = if (Test-Path $archPath) { Get-Content -LiteralPath $archPath -Raw } else { '' }
$readme = if (Test-Path $readmePath) { Get-Content -LiteralPath $readmePath -Raw } else { '' }

# (a) every package directory is a row in ARCHITECTURE.md's package table, and
#     every row in that table is a real directory.
$pkgDirs = @(Get-ChildItem -Path 'packages' -Directory | Select-Object -ExpandProperty Name)
$pkgRows = @([regex]::Matches($arch, '(?m)^\|\s*`([a-z0-9][a-z0-9-]*)`\s*\|\s*(?:[0-9]+|app)\s*\|') |
    ForEach-Object { $_.Groups[1].Value })
foreach ($d in $pkgDirs) {
    if ($pkgRows -notcontains $d) {
        $claimViolations += "$archPath : package '$d' exists but has no row in the Packages table"
    }
}
foreach ($r in $pkgRows) {
    if ($pkgDirs -notcontains $r) {
        $claimViolations += "$archPath : Packages table lists '$r', which is not a directory under packages/"
    }
}

# (b) every option(ENGINE_*) is in the CMake-options table of both docs that
#     publish one. A missing option reads as a capability the build does not have.
$cmakeOpts = @([regex]::Matches((Get-Content -LiteralPath 'CMakeLists.txt' -Raw),
    'option\((ENGINE_[A-Z0-9_]+)') | ForEach-Object { $_.Groups[1].Value })
foreach ($pair in @(@($archPath, $arch), @($readmePath, $readme))) {
    $docName = $pair[0]
    $docText = $pair[1]
    $listed = @([regex]::Matches($docText, '(?m)^\|\s*`(ENGINE_[A-Z0-9_]+)`\s*\|') |
        ForEach-Object { $_.Groups[1].Value })
    foreach ($o in $cmakeOpts) {
        if ($listed -notcontains $o) {
            $claimViolations += "$docName : CMake option '$o' exists but is not in the options table"
        }
    }
    foreach ($l in $listed) {
        if ($cmakeOpts -notcontains $l) {
            $claimViolations += "$docName : options table lists '$l', which no option() declares"
        }
    }
}

# (c) every implementation package of an interface is named on that interface's
#     row in the interface/implementation table. This is what left `rhi-vulkan`
#     invisible in the table a reader consults to answer "what backends exist".
#     `assets` is exempt: it has three documented non-substitutable exceptions
#     in the prose directly under that table, so the row cannot list them all.
$ifaceExempt = @('assets')
# Scope to the interface/implementation table. `rhi` also has a row in the
# Packages table above it, and that row comes first - matching it instead was
# this check's own first bug, and it reported every interface as broken.
$ifaceSection = ''
$ifaceStart = $arch.IndexOf('## Interface / implementation pattern')
if ($ifaceStart -ge 0) {
    $ifaceRest = $arch.Substring($ifaceStart + 4)
    $ifaceEnd = $ifaceRest.IndexOf("`n## ")
    $ifaceSection = if ($ifaceEnd -ge 0) { $ifaceRest.Substring(0, $ifaceEnd) } else { $ifaceRest }
}
foreach ($iface in @($pkgDirs | Where-Object { $Layers[$_] -eq 2 })) {
    if ($ifaceExempt -contains $iface) { continue }
    $impls = @($pkgDirs | Where-Object { $_ -like "$iface-*" })
    if ($impls.Count -eq 0) { continue }
    $row = [regex]::Match($ifaceSection, "(?m)^\|\s*``$iface``\s*\|(.+)$")
    if (-not $row.Success) {
        $claimViolations += "$archPath : interface '$iface' has no row in the interface/implementation table"
        continue
    }
    foreach ($impl in $impls) {
        if ($row.Groups[1].Value -notmatch [regex]::Escape($impl)) {
            $claimViolations += ("$archPath : '$impl' implements '$iface' but is not named on " +
                "that row of the interface/implementation table")
        }
    }
}

Add-Result 'doc-claims' `
    ("$($pkgDirs.Count) packages, $($cmakeOpts.Count) CMake options, " +
     'interface rows complete') `
    $claimViolations


# ── 19. Every command a doc tells you to run exists ──────────────────────────
# A backticked `/name` is an instruction: run this. When a skill is deleted or
# renamed, those instructions become traps that cost a session a wrong turn
# before it notices. Seven skills were retired at once when the management
# service took over their jobs, leaving stale invocations in four live docs.
#
# Only backticked references count, and that distinction is deliberate. Prose
# naming a past run ("the 29 Aug analizeMax audit") is history and stays true
# after the command is gone; a backticked command is a present-tense claim that
# you can run it.
#
# docs/superpowers/ is excluded for the same reason doc-links excludes it: specs
# and plans are dated archives, and an instruction that was correct when written
# is allowed to age with the document that carries it.
$refViolations = @()
$skillDirs = @()
if (Test-Path '.claude/skills') {
    $skillDirs = @(Get-ChildItem -Path '.claude/skills' -Directory |
        Select-Object -ExpandProperty Name)
}

# Slash tokens that are not commands. Mount roots read identically to a skill
# name, and there is no structural way to tell them apart - so they are listed.
$notCommands = @(
    'content', 'shaders', 'textures', 'meshes', 'debug', 'logs',
    'register-space', 'api'
)

$refScanned = 0
$refMd = @(Get-ChildItem -Path . -Recurse -File -Filter '*.md' -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch '[\\/]\.git[\\/]' -and
        $_.FullName -notmatch '[\\/]node_modules[\\/]' -and
        $_.FullName -notmatch '[\\/]build[\\/]' -and
        $_.FullName -notmatch '[\\/]third_party[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]superpowers[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]analysis[\\/]2[0-9]{3}-'
    })
foreach ($f in $refMd) {
    $refScanned++
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $rel = $f.FullName.Substring($repo.Length + 1).Replace('\', '/')
    foreach ($m in [regex]::Matches($text, '`/([A-Za-z][A-Za-z0-9-]{2,})(?=[ `])')) {
        $name = $m.Groups[1].Value
        if ($notCommands -contains $name) { continue }
        if ($skillDirs -contains $name) { continue }
        $refViolations += "$rel : ``/$name`` is not a skill under .claude/skills/"
    }
}

Add-Result 'doc-skill-refs' `
    "$refScanned live docs, every ``/command`` resolves to one of $($skillDirs.Count) skills" `
    $refViolations


# ── 20. VISION.md's contradiction table matches the tree ─────────────────────
# VISION.md carries a table of decisions already made *against* the stated goal
# - the 512-instance cap, index-keyed motion history, and so on. That table is
# the measured gap between what this is and what it is meant to become, and it
# is only useful while it is true.
#
# Two ways it rots, and this catches both. A contradiction that gets fixed
# leaves a stale row claiming the tree is worse than it is - so a row whose
# symbol no longer exists **fails**, and the fix is to delete the row and
# celebrate. A row that names the wrong file fails the same way, which keeps the
# table's references usable rather than decorative.
#
# It deliberately does not check the two contradictions VISION.md lists as
# having no symbol (no despawn, monolithic scene file): an absence cannot be
# grepped for without inventing a pattern that would go green for the wrong
# reason.
$visionViolations = @()
$visionPath = 'VISION.md'
$visionRows = 0
if (Test-Path $visionPath) {
    $visionText = Get-Content -LiteralPath $visionPath -Raw
    # | description | `path` | `symbol` |
    $rowPattern = '(?m)^\|\s*[^|]+\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|\s*$'
    foreach ($m in [regex]::Matches($visionText, $rowPattern)) {
        $file = $m.Groups[1].Value.Trim()
        $symbol = $m.Groups[2].Value.Trim()
        # Skip the decisions table, whose second column is prose not a path.
        if ($file -notmatch '^packages/') { continue }
        $visionRows++
        if (-not (Test-Path $file)) {
            $visionViolations += ("$visionPath : row names '$file', which does not exist - " +
                'update the path or delete the row')
            continue
        }
        $body = Get-Content -LiteralPath $file -Raw
        if ($body -notmatch ('(?m)^\s*(?:inline\s+)?constexpr\s+\w+\s+' +
                [regex]::Escape($symbol) + '\s*=')) {
            $visionViolations += ("$visionPath : '$symbol' is gone from $file - the " +
                'contradiction is resolved, so delete the row')
        }
    }
    if ($visionRows -eq 0) {
        $visionViolations += "$visionPath : no contradiction rows parsed - has the table moved?"
    }
}
else {
    $visionViolations += 'VISION.md is missing - it is the single owner of the product goal'
}

Add-Result 'vision-gap' `
    "$visionRows named contradictions in VISION.md, all still present in the tree" `
    $visionViolations


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
