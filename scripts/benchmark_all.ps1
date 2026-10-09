# The whole performance report in one go (run_benchmark.bat full): every part of
# docs/benchmark.md that a report needs, one after the other, unattended, and one
# shareable zip at the end.
#
#   1. What each part costs and what limits this PC: a Time Attack stepped 1/60 s a
#      frame (-Scenario solo -FixedStep), baseline, skip-draws, render-50 and no-audio, -Repeats rounds. Its warm-up checks that the menus reach the race: when
#      they do not, everything stops there (benchmark.ps1 says why).
#   2. The frame rate a player gets: a Free Race with rivals, by elapsed time,
#      -RaceRepeats rounds.
#
# out\bench\<time>-full holds both runs (parts\ and race\) and report.md, the two
# summaries in one file to paste whole; out\bench\<time>-full-shareable.zip is the one
# file to send. About 45 minutes on an i5-3470.
#
# -Set cache (run_benchmark.bat cache) is the cache-budget set instead, all Time Attack
# stepped 1/60 s a frame:
#   1. index: baseline, no-index-cache, constant-reuse and no-draw-timers, -Repeats rounds.
#   2. hog: baseline and hog-64 to hog-12288 (how much a frame depends on the L3), -HogRepeats rounds.
#   3. pmc: baseline and skip-draws once each with the CPU's counters recorded
#      (benchmark.ps1 -RecordPmc), from an administrator PowerShell only.
param(
    [ValidateSet('full', 'cache')][string]$Set = 'full',
    [ValidateRange(1, 100)][int]$Repeats = 4,
    [ValidateRange(1, 100)][int]$RaceRepeats = 3,
    [ValidateRange(1, 100)][int]$HogRepeats = 3,
    [string]$ImageDirectory = '',
    [string]$AssetDirectory = '',
    [string]$SaveDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root ('out/bench/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Set")
New-Item -ItemType Directory -Path $out | Out-Null
$common = @('-SkipBuildCheck')
foreach ($pair in @(@('ImageDirectory', $ImageDirectory), @('AssetDirectory', $AssetDirectory), @('SaveDirectory', $SaveDirectory))) {
    if ($pair[1]) { $common += @("-$($pair[0])", $pair[1]) }
}
# The report's headings are in benchmark_summary.py: this file stays ASCII, as Windows
# PowerShell reads a script without a byte order mark in the system's code page.
$phases = @(
    @{ name = 'parts'
       arguments = @('-Scenario', 'solo', '-FixedStep', '-Configs', 'baseline,skip-draws,render-50,no-audio',
                     '-AllowDiagnosticRendering', '-Repeats', "$Repeats") },
    @{ name = 'race'
       arguments = @('-Configs', 'baseline', '-Repeats', "$RaceRepeats") }
)
if ($Set -eq 'cache') {
    $solo = @('-Scenario', 'solo', '-FixedStep')
    $phases = @(
        @{ name = 'index'; arguments = $solo + @('-Configs', 'baseline,no-index-cache,constant-reuse,no-draw-timers', '-Repeats', "$Repeats") },
        @{ name = 'hog'; arguments = $solo + @('-Configs', 'baseline,hog-64,hog-2048,hog-4096,hog-12288', '-Repeats', "$HogRepeats") }
    )
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($admin) {
        $phases += @{ name = 'pmc'; arguments = $solo + @('-Configs', 'baseline,skip-draws', '-Repeats', '1', '-AllowDiagnosticRendering', '-RecordPmc') }
    } else { Write-Warning 'Not an administrator PowerShell: the CPU-counter part (pmc) is left out.' }
}
$started = Get-Date
foreach ($phase in $phases) {
    Write-Output ''
    Write-Output ("=== {0}/{1} {2} (started {3:HH:mm}) ===" -f ([array]::IndexOf($phases, $phase) + 1), $phases.Count, $phase.name, (Get-Date))
    $target = Join-Path $out $phase.name
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'benchmark.ps1') @common @($phase.arguments) -Out $target
    if ($LASTEXITCODE -or -not (Test-Path -LiteralPath (Join-Path $target 'summary.md'))) {
        throw "The $($phase.name) runs did not finish (see above, and $target): the rest was not run."
    }
}

# One file to paste: the PC once, then each part's summary (benchmark_summary.py --report).
# The py launcher first: 'python' may be the Microsoft Store's stand-in, which runs nothing.
$minutes = [int]((Get-Date) - $started).TotalMinutes
foreach ($python in @(@('py', '-3'), @('python'))) {
    if (-not (Get-Command $python[0] -ErrorAction SilentlyContinue)) { continue }
    $rest = @($python | Select-Object -Skip 1)
    & $python[0] @rest (Join-Path $PSScriptRoot 'benchmark_summary.py') $out --report --set $Set --minutes $minutes | Out-Null
    if (Test-Path -LiteralPath (Join-Path $out 'report.md')) { break }
}
if (-not (Test-Path -LiteralPath (Join-Path $out 'report.md'))) { throw "No Python wrote report.md: the summaries are in $out\parts and $out\race." }

# One zip to send: report.md and both runs' shareable copies (anonymize_benchmark.py,
# which benchmark.ps1 already ran on each).
$stage = Join-Path $out 'shareable'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $out 'report.md') -Destination $stage
foreach ($phase in $phases) {
    $copy = Join-Path $out "$($phase.name)-shareable"
    if (Test-Path -LiteralPath $copy) { Copy-Item -Recurse -LiteralPath $copy -Destination (Join-Path $stage $phase.name) }
    else { Write-Warning "No shareable copy of the $($phase.name) runs (is Python installed?): only report.md goes in the zip." }
}
$zip = "$out-shareable.zip"
# tar (Windows 10 1803 and later) writes a zip with forward slashes; Windows PowerShell 5.1's
# Compress-Archive writes backslashes, which other systems take for part of the file name.
if (Get-Command tar -ErrorAction SilentlyContinue) {
    & tar -a -c -f $zip -C $stage *
    if ($LASTEXITCODE) { throw "tar could not write $zip" }
} else { Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip }
$mb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
Write-Output ''
Write-Output "Done in $minutes minutes. Paste $out\report.md into the report form and attach $zip ($mb MB)."
if ($mb -gt 25) { Write-Warning "The zip is over 25 MB: GitHub may refuse it. Attach the -shareable zips inside $out\parts* and $out\race* instead." }
