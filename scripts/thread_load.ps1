# How busy each logical processor and each thread of a running program is, so a
# race in this port and the same race in Xenia can be put side by side on one PC:
# is one thread saturated, and how much of the other cores is in use.
#
#   scripts\thread_load.ps1 -Label xenia                   # starts sampling at once
#   scripts\thread_load.ps1 -ProcessName sfr_cpu_diagnostic -Delay 5 -Seconds 60
#
# Start the race first (Free Race, same course and character, nobody at the
# controls), then start the sampling once the countdown is over. The window title
# is recorded each second as well: Xenia shows its frame rate there.
# Nothing here names the PC or the user; the folder can be sent as it is.
param(
    [string]$ProcessName = '',             # without .exe; empty: the first of the known names that is running
    [ValidateRange(5, 3600)][int]$Seconds = 60,
    [ValidateRange(0.25, 10.0)][double]$Interval = 1.0,
    [int]$Delay = 0,                       # seconds to wait before sampling; -1: wait for Enter
    [string]$Label = '',                   # a word for the folder name: xenia, port, ...
    [string]$Out = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$known = @('xenia_canary', 'xenia', 'sfr_cpu_diagnostic', 'sfr_cpu_diagnostic_a', 'sfr_cpu_diagnostic_b')
$process = $null
if ($ProcessName) {
    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $process) { throw "No running program named $ProcessName (start it and the race first)" }
} else {
    foreach ($name in $known) {
        $process = Get-Process -Name $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($process) { break }
    }
    if (-not $process) { throw "None of $($known -join ', ') is running: start the race first, or name the program with -ProcessName" }
}
if (-not $Label) { $Label = $process.ProcessName }
Write-Output "Program: $($process.ProcessName) (pid $($process.Id))"

if ($Delay -lt 0) { Read-Host 'Press Enter when the race is under way (after the countdown)' | Out-Null }
elseif ($Delay -gt 0) { Start-Sleep -Seconds $Delay }

if (-not $Out) { $Out = Join-Path $root ('out/thread-load/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Label") }
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $Out) { throw "Output already exists: $Out" }
New-Item -ItemType Directory -Path $Out | Out-Null

# Win32_PerfFormattedData is not translated with the Windows language, unlike
# the counter paths Get-Counter takes (\Processor(*)\... fails on a Chinese Windows).
function Get-CoreLoad {
    $result = [ordered]@{}
    foreach ($row in Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor) {
        if ($row.Name -ne '_Total') { $result[[int]$row.Name] = [double]$row.PercentProcessorTime }
    }
    $result
}
function Get-ThreadTimes([System.Diagnostics.Process]$p) {
    $times = @{}
    $p.Refresh()
    foreach ($thread in $p.Threads) {
        try { $times[$thread.Id] = $thread.TotalProcessorTime.TotalMilliseconds } catch { }
    }
    $times
}

$coreRows = New-Object System.Collections.Generic.List[string]
$titleRows = New-Object System.Collections.Generic.List[string]
$coreSum = @{}; $coreMax = @{}; $coreSamples = 0
$first = Get-ThreadTimes $process
$firstClock = [Diagnostics.Stopwatch]::StartNew()
$samples = [int][math]::Ceiling($Seconds / $Interval)
$header = $null
Write-Output "Sampling $Seconds s ..."
for ($i = 1; $i -le $samples; ++$i) {
    Start-Sleep -Milliseconds ([int]($Interval * 1000))
    if ($process.HasExited) { Write-Output 'The program ended during the sampling.'; break }
    $cores = Get-CoreLoad
    if (-not $header) { $header = 'second,' + (($cores.Keys | ForEach-Object { "cpu$_" }) -join ','); $coreRows.Add($header) }
    $coreRows.Add(('{0:F1},' -f $firstClock.Elapsed.TotalSeconds) + (($cores.Values | ForEach-Object { '{0:F0}' -f $_ }) -join ','))
    foreach ($key in $cores.Keys) {
        $coreSum[$key] = $coreSum[$key] + $cores[$key]
        if (-not $coreMax.ContainsKey($key) -or $cores[$key] -gt $coreMax[$key]) { $coreMax[$key] = $cores[$key] }
    }
    ++$coreSamples
    $process.Refresh()
    $titleRows.Add(('{0:F1} {1}' -f $firstClock.Elapsed.TotalSeconds, $process.MainWindowTitle))
}
$last = Get-ThreadTimes $process
$wall = $firstClock.Elapsed.TotalMilliseconds

# A thread's share of one logical processor over the whole sampling.
$threads = foreach ($id in $last.Keys) {
    $before = if ($first.ContainsKey($id)) { $first[$id] } else { 0 }
    [pscustomobject]@{ Thread = $id; Busy = [math]::Round(100 * ($last[$id] - $before) / $wall, 1); New = -not $first.ContainsKey($id) }
}
$threads = @($threads | Sort-Object Busy -Descending)
@('thread,busy_percent_of_one_cpu,started_during_sampling') + @($threads | ForEach-Object { "$($_.Thread),$($_.Busy),$($_.New)" }) |
    Set-Content -LiteralPath (Join-Path $Out 'threads.csv') -Encoding UTF8
$coreRows | Set-Content -LiteralPath (Join-Path $Out 'cores.csv') -Encoding UTF8
$titleRows | Set-Content -LiteralPath (Join-Path $Out 'titles.txt') -Encoding UTF8

# Frame rates the title shows ("60 FPS", "FPS: 59.9", ...).
$fps = @($titleRows | ForEach-Object {
    if ($_ -match '(\d+(?:\.\d+)?)\s*FPS' -or $_ -match 'FPS[^\d]{0,3}(\d+(?:\.\d+)?)') { [double]$Matches[1] }
})

$cpu = try { (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim() } catch { 'unknown' }
$gpu = try { (Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name) -join '; ' } catch { 'unknown' }
$busyTotal = ($threads | Measure-Object Busy -Sum).Sum
$lines = @(
    "# Thread load: $Label",
    '',
    "- program: $($process.ProcessName), sampled $([math]::Round($wall / 1000, 1)) s every $Interval s",
    "- cpu: $cpu",
    "- gpu: $gpu",
    "- os: $([Environment]::OSVersion.VersionString)",
    "- logical processors: $($coreSum.Count)",
    "- whole program: $([math]::Round($busyTotal, 0))% of one logical processor ($([math]::Round($busyTotal / [math]::Max(1, $coreSum.Count), 0))% of the PC)",
    $(if ($fps.Count) { "- frame rate in the title: mean $([math]::Round(($fps | Measure-Object -Average).Average, 1)), min $(($fps | Measure-Object -Minimum).Minimum), max $(($fps | Measure-Object -Maximum).Maximum) ($($fps.Count) samples)" } else { '- frame rate in the title: none found (titles.txt)' }),
    '',
    '## Busiest threads (percent of one logical processor)',
    '',
    '| thread | busy % |',
    '| ---: | ---: |'
)
$lines += @($threads | Select-Object -First 15 | ForEach-Object { "| $($_.Thread) | $($_.Busy) |" })
$lines += @('', '## Logical processors (percent busy)', '', '| cpu | mean | max |', '| ---: | ---: | ---: |')
$lines += @($coreSum.Keys | Sort-Object | ForEach-Object { "| $_ | $([math]::Round($coreSum[$_] / [math]::Max(1, $coreSamples), 0)) | $([math]::Round($coreMax[$_], 0)) |" })
$lines += @('', 'A thread near 100% is a whole processor: the program cannot go faster than it, however idle the others are.')
$lines | Set-Content -LiteralPath (Join-Path $Out 'summary.md') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $Out 'summary.md') | Select-Object -First 30
Write-Output ''
Write-Output "Written to $Out"
