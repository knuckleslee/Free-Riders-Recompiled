# One short race with each build and setting there is, to see that each of them
# works before any is timed (scripts\night.ps1 does the timing):
#
#   run_benchmark.bat smoke            (or scripts\smoke.ps1)
#
# baseline, every sfr_cpu_diagnostic_<a-e>.exe in out\build\host, the main core
# reserved, every guest thread in parallel (with and without the main core, and
# the stress setting) and the profile. scripts\smoke_check.py then says for each
# whether it ended normally, reached and drew the race, and left the marks of
# its feature, in smoke.md: send that file. About 10 to 12 runs of 4 to 5 minutes.
#
# Each race is as long as the benchmark's (-AfterSay 4200 presents after the last
# word): the game counts some 25 s of start line and countdown as racing, and a
# limit in presents lasts fewer seconds the faster a setting draws them, so 900
# (the first smoke run) ended every race inside that scene, all mode within 5 s.
#
# -AllOnly runs only the builds in all mode (exe-a-all ... exe-e-all), for when the
# rest already passed.
param(
    [ValidateRange(300, 100000)][int]$AfterSay = 4200,
    [switch]$AllOnly,
    [string]$ImageDirectory = '',
    [string]$AssetDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$builds = @(foreach ($letter in 'a', 'b', 'c', 'd', 'e') {
    if (Test-Path -LiteralPath (Join-Path $host_dir "sfr_cpu_diagnostic_$letter.exe")) { $letter }
})
$inAll = @($builds | ForEach-Object { "exe-$_-all" })
if ($AllOnly) {
    if (-not $inAll.Count) { throw 'No sfr_cpu_diagnostic_<a-e>.exe in out\build\host' }
    $configs = $inAll
} else {
    $configs = @('baseline') + @($builds | ForEach-Object { "exe-$_" }) +
               @('main-core', 'all', 'all-main-core', 'all-stress', 'profile') + $inAll
}
$out = Join-Path $root ('out/bench/smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
Write-Output "Smoke run: $($configs -join ', ') (one short race each)"
$arguments = @{ SkipBuildCheck = $true; Configs = $configs; Repeats = 1; NoWarmup = $true; AfterSay = $AfterSay; TimeoutMinutes = 10; Out = $out }
if ($ImageDirectory) { $arguments['ImageDirectory'] = $ImageDirectory }
if ($AssetDirectory) { $arguments['AssetDirectory'] = $AssetDirectory }
& (Join-Path $PSScriptRoot 'benchmark.ps1') @arguments

$checked = $false
foreach ($python in @(@('py', '-3'), @('python'))) {
    if (-not (Get-Command $python[0] -ErrorAction SilentlyContinue)) { continue }
    $rest = @($python | Select-Object -Skip 1)
    Write-Output ''
    & $python[0] @rest (Join-Path $PSScriptRoot 'smoke_check.py') $out
    if (Test-Path -LiteralPath (Join-Path $out 'smoke.md')) { $checked = $true; break }
}
Write-Output ''
if ($checked) {
    # Beside the run folder too, where it is easy to find and send.
    Copy-Item -LiteralPath (Join-Path $out 'smoke.md') -Destination "$out-smoke.md"
    Write-Output "Send this file: $out-smoke.md"
} else {
    Write-Output "No Python ran the check: send the $out-shareable zip(s) instead."
}
