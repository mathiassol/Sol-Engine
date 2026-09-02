#!/usr/bin/env pwsh
#
# Wrapper for the AI Management CLI, so a skill has one thing to call that
# always resolves.
#
# `npm link` puts an `aim` shim in npm's global directory, but that directory is
# not always on PATH in a non-interactive shell — which is exactly where the
# skills run. Rather than have every skill hope, resolve it here in three steps
# and fail with an instruction if none work.
#
#   pwsh -NoProfile -File tools/aim.ps1 doctor
#   pwsh -NoProfile -File tools/aim.ps1 whatnow
#   pwsh -NoProfile -File tools/aim.ps1 audit submit .\audit.json
#
# Arguments and the exit code pass through untouched: a non-zero exit means the
# command failed, and the CLI's message is the one to read.
#
[CmdletBinding()]
# NOT $Args: that shadows PowerShell's automatic $args and the call below
# then loses the exit code, so a failed command looked like a success.
param([Parameter(ValueFromRemainingArguments = $true)] [string[]]$Passthru)

$ErrorActionPreference = 'Stop'

function Resolve-Aim {
    # 1. On PATH, which is how it should be.
    $onPath = Get-Command aim -ErrorAction SilentlyContinue
    if ($onPath) { return @{ Exe = $onPath.Source; Pre = @() } }

    # 2. npm's global shim, wherever npm says that is.
    try {
        $prefix = (& npm prefix -g 2>$null | Select-Object -First 1)
        if ($prefix) {
            foreach ($name in @('aim.cmd', 'aim.ps1', 'aim')) {
                $candidate = Join-Path $prefix.Trim() $name
                if (Test-Path -LiteralPath $candidate) { return @{ Exe = $candidate; Pre = @() } }
            }
        }
    } catch { }

    # 3. The sibling checkout, run through node. Works with no install at all.
    $sibling = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'AI-Mangment\cli\bin\aim.mjs'
    if (Test-Path -LiteralPath $sibling) {
        $node = Get-Command node -ErrorAction SilentlyContinue
        if ($node) { return @{ Exe = $node.Source; Pre = @($sibling) } }
    }

    return $null
}

$aim = Resolve-Aim
if (-not $aim) {
    Write-Host 'aim: not found. Tried PATH, npm''s global directory, and ..\AI-Mangment\cli.' -ForegroundColor Red
    Write-Host 'Install it: cd ..\AI-Mangment\cli; npm link' -ForegroundColor Yellow
    exit 127
}

$argv = @()
if ($aim.Pre)      { $argv += $aim.Pre }
if ($Passthru)     { $argv += $Passthru }
& $aim.Exe @argv

# A null $LASTEXITCODE would become 0 and report a failure as success.
if ($null -eq $LASTEXITCODE) { exit 1 }
exit $LASTEXITCODE
