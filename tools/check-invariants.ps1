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

# ── 11. The analizeMax analysis set is internally consistent ─────────────────
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
        if ($hubGrades.ContainsKey($key)) {
            $own = ([regex]::Match($body, '(?m)^\*\*Grade:\s*([^*]+?)\s*\*\*')).Groups[1].Value
            if ($own -and $own.Trim() -ne $hubGrades[$key]) {
                $analysisViolations += "$analysisDir/$($mf.Name): grade '$($own.Trim())' disagrees with LATEST.md's '$($hubGrades[$key])' - one was written against a different audit"
            }
        }
    }

    $published = @($urls).Count
    $metricFiles = @(Get-ChildItem -Path $analysisDir -File -Filter 'metric-*.md' -ErrorAction SilentlyContinue).Count
    $analysisSummary = "registry valid, $published/9 published, $metricFiles/6 metric pages"
}
Add-Result 'analysis-set' $analysisSummary $analysisViolations

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
