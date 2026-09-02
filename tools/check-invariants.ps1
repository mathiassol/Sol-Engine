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
    'platform-win32' = 3; 'rhi-d3d12' = 3; 'rhi-vulkan' = 3; 'shaders-dxc' = 3
    'audio-xaudio2' = 3
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
#   docs/analysis/     - generated audit reports from /analizeMax. Same reason
#                        as superpowers/: dated snapshots, and a report five
#                        runs old may reference a file that has since moved. A
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

# ── 12. The analizeMax analysis set is internally consistent ─────────────────
# The publishing contract (.claude/skills/analizeMax/publishing.md) is ~200
# lines of prose that three skills are asked to obey. The half of it that is
# machine-checkable should not be prose: a duplicate artifact URL means one
# document has been silently overwriting another, and a grade that disagrees
# between the hub and the page it links to is the one failure a reader cannot
# detect for themselves.
#
# Consistency, not completeness. A fresh clone has the registry (committed) but
# no reports (generated, untracked), and that is a valid state — so completeness
# is only enforced once LATEST.md proves an audit has actually run here.
$analysisViolations = @()
$analysisDir = 'docs/analysis'
# Newest full report, by name - the filenames are date-prefixed. A metric page
# whose `derived_from` matches this is current; an older one is superseded and
# that is expected, because /analizeMax does not regenerate metric pages.
$newestFull = ''
if (Test-Path $analysisDir) {
    $fulls = @(Get-ChildItem -Path $analysisDir -File -Filter '*-full.md' -ErrorAction SilentlyContinue |
        Sort-Object Name)
    if ($fulls.Count -gt 0) { $newestFull = $fulls[-1].Name }
}
$metricKeys = @('stability', 'architecture', 'capabilities', 'portability', 'devex', 'ai-tooling')
$registryPath = Join-Path $analysisDir 'artifacts.json'
$analysisSummary = 'no analysis set present'

if (Test-Path $registryPath) {
    $registry = $null
    try {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
    } catch {
        $analysisViolations += "$analysisDir/artifacts.json: not valid JSON - $($_.Exception.Message)"
    }

    if ($registry) {
        foreach ($key in @('hub', 'full', 'roadmap')) {
            if (-not $registry.PSObject.Properties.Name.Contains($key)) {
                $analysisViolations += "$analysisDir/artifacts.json: missing '$key' entry"
            }
        }
        if (-not $registry.PSObject.Properties.Name.Contains('metrics')) {
            $analysisViolations += "$analysisDir/artifacts.json: missing 'metrics' object"
        } else {
            foreach ($key in $metricKeys) {
                if (-not $registry.metrics.PSObject.Properties.Name.Contains($key)) {
                    $analysisViolations += "$analysisDir/artifacts.json: metrics is missing '$key'"
                }
            }
            foreach ($extra in $registry.metrics.PSObject.Properties.Name) {
                if ($extra -notin $metricKeys) {
                    $analysisViolations += "$analysisDir/artifacts.json: unknown metric key '$extra'"
                }
            }
        }

        # Every entry needs a favicon and title (both fixed at creation), and a
        # url field even when empty. A duplicate url is the serious one.
        $entries = @()
        foreach ($key in @('hub', 'full', 'roadmap')) {
            if ($registry.PSObject.Properties.Name.Contains($key)) {
                $entries += [pscustomobject]@{ Name = $key; Entry = $registry.$key }
            }
        }
        if ($registry.PSObject.Properties.Name.Contains('metrics')) {
            foreach ($key in $registry.metrics.PSObject.Properties.Name) {
                $entries += [pscustomobject]@{ Name = "metrics.$key"; Entry = $registry.metrics.$key }
            }
        }
        foreach ($e in $entries) {
            foreach ($field in @('url', 'favicon', 'title')) {
                if (-not $e.Entry.PSObject.Properties.Name.Contains($field)) {
                    $analysisViolations += "$analysisDir/artifacts.json: $($e.Name) has no '$field'"
                }
            }
        }
        $urls = @($entries | ForEach-Object { $_.Entry.url } | Where-Object { $_ })
        foreach ($dup in ($urls | Group-Object | Where-Object { $_.Count -gt 1 })) {
            $analysisViolations += "$analysisDir/artifacts.json: $($dup.Count) entries share the URL $($dup.Name) - one has been overwriting the other"
        }
    }

    # Grades in the hub, if an audit has run here.
    $latestPath = Join-Path $analysisDir 'LATEST.md'
    $hubGrades = @{}
    $rowToKey = @{
        'Stability' = 'stability'; 'Architecture' = 'architecture'
        'Capabilities' = 'capabilities'; 'Cross-platform readiness' = 'portability'
        'Developer setup' = 'devex'; 'AI tooling' = 'ai-tooling'
    }
    if (Test-Path $latestPath) {
        foreach ($line in Get-Content -LiteralPath $latestPath) {
            if ($line -notmatch '^\|') { continue }
            $cells = @($line -split '\|' | ForEach-Object { $_.Trim() })
            if ($cells.Count -lt 4) { continue }
            $label = $cells[1]
            if (-not $rowToKey.ContainsKey($label)) { continue }
            $grade = ($cells[2] -replace '\*', '').Trim()
            if ($grade) { $hubGrades[$rowToKey[$label]] = $grade }
        }
        foreach ($key in $metricKeys) {
            if (-not $hubGrades.ContainsKey($key)) {
                $analysisViolations += "$analysisDir/LATEST.md: scorecard has no grade row for '$key'"
            }
            # An audit has run, so every metric must have a page to link to -
            # a placeholder counts. A hub link that dead-ends is not shareable.
            if (-not (Test-Path (Join-Path $analysisDir "metric-$key.md"))) {
                $analysisViolations += "$analysisDir/metric-$key.md: missing, but LATEST.md exists (run /analizeMax-repair)"
            }
        }
    }

    # Each metric file that does exist: known key, valid state, agreeing grade.
    foreach ($mf in Get-ChildItem -Path $analysisDir -File -Filter 'metric-*.md' -ErrorAction SilentlyContinue) {
        $key = $mf.BaseName -replace '^metric-', ''
        if ($key -notin $metricKeys) {
            $analysisViolations += "$analysisDir/$($mf.Name): '$key' is not one of the six metric keys"
            continue
        }
        $body = Get-Content -LiteralPath $mf.FullName -Raw
        $state = ([regex]::Match($body, '(?m)^state:\s*(\S+)\s*$')).Groups[1].Value
        if (-not $state) {
            $analysisViolations += "$analysisDir/$($mf.Name): no 'state:' in frontmatter (expected report or empty)"
        } elseif ($state -notin @('report', 'empty')) {
            $analysisViolations += "$analysisDir/$($mf.Name): unrecognised state '$state' (use report or empty)"
        }
        # Grade agreement, but only against the audit this page actually derives
        # from. /analizeMax deliberately does not regenerate metric pages - it
        # would cost six designed pages nobody asked to read - so a page from an
        # older audit legitimately carries an older grade. What matters is that
        # `derived_from` says so; the hub labels that row superseded.
        $derived = ([regex]::Match($body, '(?m)^derived_from:\s*(\S+)\s*$')).Groups[1].Value
        if (-not $derived) {
            $analysisViolations += "$analysisDir/$($mf.Name): no 'derived_from:' in frontmatter - nothing can tell which audit this page is from"
        } elseif ($newestFull -and $derived -ne $newestFull) {
            # Older audit: stale by design, but it must name a report that exists.
            if (-not (Test-Path (Join-Path $analysisDir $derived))) {
                $analysisViolations += "$analysisDir/$($mf.Name): derived_from '$derived' is not a report in this directory"
            }
        } elseif ($hubGrades.ContainsKey($key)) {
            $own = ([regex]::Match($body, '(?m)^\*\*Grade:\s*([^*]+?)\s*\*\*')).Groups[1].Value
            if ($own -and $own.Trim() -ne $hubGrades[$key]) {
                $analysisViolations += "$analysisDir/$($mf.Name): derives from the current audit but its grade '$($own.Trim())' disagrees with LATEST.md's '$($hubGrades[$key])'"
            }
        }
    }

    # Distinguish real reports from placeholders. "6/6 metric pages" reads as
    # complete when five of them may be 80-word stubs carrying only a grade -
    # a valid state since /analizeMax stopped generating them, but not the same
    # thing, and the summary line is what anyone actually reads.
    $published = @($urls).Count
    $reportCount = 0
    $emptyCount = 0
    foreach ($mf2 in Get-ChildItem -Path $analysisDir -File -Filter 'metric-*.md' -ErrorAction SilentlyContinue) {
        $st2 = ([regex]::Match((Get-Content -LiteralPath $mf2.FullName -Raw), '(?m)^state:\s*(\S+)\s*$')).Groups[1].Value
        if ($st2 -eq 'report') { $reportCount++ } elseif ($st2 -eq 'empty') { $emptyCount++ }
    }
    $analysisSummary = "registry valid, $published/9 published, $reportCount report + $emptyCount placeholder"
}
Add-Result 'analysis-set' $analysisSummary $analysisViolations

# ── 13. Conditionally-added packages are only ever linked conditionally ──────
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

# ── 14. The tree obeys the .editorconfig it ships ────────────────────────────
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

# ── 15. Every gate is declared and classified ────────────────────────────────
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

# ── 16. The RHI interface speaks no backend's vocabulary ─────────────────────
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
