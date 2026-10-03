# Every pending comparison, one after another, for a night on the benchmark PC:
#
#   run_benchmark.bat night                     (or scripts\night.ps1 [-Steps pgo,stab])
#
#   pgo    the usual build against the profile-guided _e        (when _e is there)
#   stab   many short races with every guest thread in parallel
#   exe    the builds of scripts\build_ab.ps1, _a to _d (as many as are there), all in all mode
#   core   the main thread's core kept for it (SFR_MAIN_CORE=reserve), with and without all
#   prof   where the main thread spends a race
#
# Each step is an ordinary benchmark in out\bench\night-<time>\<step>, with its own
# summary and its own shareable copy. At the end night-<time>-overview.zip holds the
# summaries of all of them, taken from the shareable copies: send that first; the
# full -shareable zips only if asked. A step whose builds are missing is skipped
# and said so; a step that fails does not stop the ones after it.
param(
    [string[]]$Steps = @('pgo', 'stab', 'exe', 'core', 'prof'),
    [string]$ImageDirectory = '',
    [string]$AssetDirectory = ''
)
$ErrorActionPreference = 'Stop'
# "pgo,stab" arrives as one string through powershell -File: split it.
$Steps = @($Steps | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($step in $Steps) {
    if ($step -notin 'pgo', 'stab', 'exe', 'core', 'prof') { throw "Unknown step '$step' (pgo, stab, exe, core, prof)" }
}
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$night = Join-Path $root ('out/bench/night-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $night | Out-Null
function Has([string]$letter) { Test-Path -LiteralPath (Join-Path $host_dir "sfr_cpu_diagnostic_$letter.exe") }

# name -> configs, rounds, extra benchmark arguments (by name: an array would pass them by position)
$plan = [ordered]@{}
foreach ($step in $Steps) {
    switch ($step) {
        'pgo'  { if (Has 'e' -and (Has 'a')) { $plan['pgo'] = @(@('exe-a-all', 'exe-e-all'), 6, @{ TimeoutMinutes = 10 }) } }
        'stab' { $plan['stab'] = @(@('all', 'all-stress'), 10, @{ AfterSay = 1500; TimeoutMinutes = 10 }) }
        'exe'  {
            # In all mode, the one most likely to become the default (core compares it with the default mode).
            $builds = @(foreach ($letter in 'a', 'b', 'c', 'd') { if (Has $letter) { "exe-$letter-all" } })
            if ($builds.Count -ge 2) { $plan['exe'] = @($builds, 6, @{ TimeoutMinutes = 10 }) }
        }
        'core' { $plan['core'] = @(@('baseline', 'main-core', 'all', 'all-main-core'), 5, @{ TimeoutMinutes = 10 }) }
        'prof' { $plan['prof'] = @(@('profile'), 2, @{}) }
    }
}
$skipped = @($Steps | Where-Object { -not $plan.Contains($_) })
$lines = @("# Night $(Split-Path -Leaf $night)", '', "commit: $(Get-Content -LiteralPath (Join-Path $root 'commit.txt') -ErrorAction SilentlyContinue)", '')
foreach ($name in $skipped) { $lines += "- $name skipped: its builds are not in out\build\host" }
foreach ($name in $plan.Keys) { $lines += "- ${name}: $($plan[$name][0] -join ', '), $($plan[$name][1]) rounds" }
$lines | ForEach-Object { Write-Output $_ }

$results = [ordered]@{}
foreach ($name in $plan.Keys) {
    $configs, $rounds, $extra = $plan[$name]
    $arguments = @{ SkipBuildCheck = $true; Configs = $configs; Repeats = $rounds; Out = (Join-Path $night $name) }
    foreach ($key in $extra.Keys) { $arguments[$key] = $extra[$key] }
    if ($ImageDirectory) { $arguments['ImageDirectory'] = $ImageDirectory }
    if ($AssetDirectory) { $arguments['AssetDirectory'] = $AssetDirectory }
    Write-Output ''
    Write-Output "=== $name ($(Get-Date -Format 'HH:mm')): $($configs -join ', '), $rounds rounds ==="
    $started = Get-Date
    try {
        & (Join-Path $PSScriptRoot 'benchmark.ps1') @arguments
        $results[$name] = "done in $([math]::Round(((Get-Date) - $started).TotalMinutes)) min"
    } catch {
        $results[$name] = "FAILED after $([math]::Round(((Get-Date) - $started).TotalMinutes)) min: $($_.Exception.Message)"
        Write-Output "  $name failed: $($_.Exception.Message); going on with the next step"
    }
}

# The overview: what each step's shareable copy says, nothing else.
$overview = "$night-overview"
New-Item -ItemType Directory -Force -Path $overview | Out-Null
$lines += @('', '## Steps', '')
foreach ($name in $results.Keys) {
    $lines += "- ${name}: $($results[$name])"
    $shared = Join-Path $night "$name-shareable"
    foreach ($file in 'summary.md', 'profile.md', 'info.txt') {
        $from = Join-Path $shared $file
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination (Join-Path $overview "$name-$file") }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $shared 'summary.md'))) { $lines += "  (no shareable summary: send $name-shareable*.zip, or the folder $name, instead)" }
}
$lines | Set-Content -LiteralPath (Join-Path $overview 'night.md') -Encoding UTF8
$zip = "$overview.zip"
Compress-Archive -Path (Join-Path $overview '*') -DestinationPath $zip -Force
Write-Output ''
$lines | Select-Object -Skip 4 | ForEach-Object { Write-Output $_ }
Write-Output ''
Write-Output "Send this file first: $zip"
