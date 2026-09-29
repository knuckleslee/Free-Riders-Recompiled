# Measures a Free Race without anyone playing: the game starts, says the menu
# words that reach a race (SFR_SAY, docs/race-controls.md), races with nobody
# at the controls and stops itself after -PresentLimit frames. Each setting
# in -Configs runs -Repeats times, taking turns, and the race frames of every
# run are summed up by scripts/benchmark_summary.py (docs/benchmark.md).
#
#   scripts\benchmark.ps1                      # the default comparison
#   scripts\benchmark.ps1 -Configs baseline -Repeats 1
#
# Build first: scripts\build_tools.ps1 -Diagnostic. The game runs with the
# launcher's defaults and nothing of yours is touched: no Kinect, no sound,
# and a copy of your save in the run's own directory.
param(
    [string[]]$Configs = @('baseline', 'sequential', 'serial', 'skip-draws'),
    [int]$Repeats = 2,
    # The menu words and the presents they are said at: title, main menu,
    # five turns of the ring to Free Race, then rules, course, character and
    # gear. By present 12000 the race is running.
    [string]$Say = 'ok@2200,ok@2600,ok@3000,start@3400,ok@3800,right@4600,right@5000,right@5400,right@5800,right@6200,ok@6600,ok@7400,ok@8200,ok@9000,ok@9800,ok@10600,ok@11400',
    [int]$PresentLimit = 15600,
    [int]$Skip = 600,                 # race frames left out at the start
    [int]$TimeoutMinutes = 25,        # a run that takes longer is stopped
    [switch]$NoWarmup,                # the first run fills the shader caches
    [switch]$Capped,                  # 60 fps as when playing, not as fast as it goes
    [string]$Out = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$exe = Join-Path $host_dir 'sfr_cpu_diagnostic.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: scripts\build_tools.ps1 -Diagnostic (missing $exe)" }

# Where the launcher found the game, else where a checkout keeps it.
$image = Join-Path $root 'out/recomp/image-loader'
$assets = Join-Path $root 'private/assets'
$ini = Join-Path $host_dir 'settings.ini'
if (Test-Path -LiteralPath $ini) {
    foreach ($line in Get-Content -LiteralPath $ini -Encoding UTF8) {
        if ($line -match '^image_directory=(.+)$') { $image = $Matches[1] }
        if ($line -match '^asset_directory=(.+)$') { $assets = $Matches[1] }
    }
}
foreach ($path in $image, $assets) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path (install the game with the launcher first)" }
}

# What each setting changes from the launcher's defaults.
$settings = @{
    'baseline'     = @{}
    'sequential'   = @{ SFR_HOST_PROCESSORS = 'sequential' }   # the old processor order
    'serial'       = @{ SFR_PARALLEL_WORKER = '0' }            # guest threads one at a time
    'all'          = @{ SFR_PARALLEL_WORKER = 'all' }          # every guest thread in parallel (experimental)
    'skip-draws'   = @{ SFR_SKIP_DRAWS = '1' }                 # the game without its drawing: the ceiling
    'render-every-2' = @{ SFR_RENDER_EVERY = '2' }             # a race drawn every other frame
    'no-vertex-cache' = @{ SFR_VERTEX_CACHE = '0' }
    'no-gpu-pipeline' = @{ SFR_GPU_PIPELINE = '0' }
    'vulkan'       = @{ SFR_GRAPHICS = 'vulkan' }
    # baseline, reporting why detached guests come back to the global permit
    # and how long they hold it (PARALLEL_HELD, summed in summary.md)
    'held'         = @{ SFR_PARALLEL_HELD = '1' }
}
# "a,b" arrives as one string through powershell -File.
$Configs = @($Configs | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($name in $Configs) {
    if (-not $settings.ContainsKey($name)) { throw "Unknown setting '$name'; known: $($settings.Keys -join ', ')" }
}

# The launcher's defaults (launcher_settings.cpp game_environment), then what
# makes a run unattended and alike.
$base = [ordered]@{
    SFR_CALL_BUDGET = '18446744073709551615'; SFR_WATCHDOG_SECONDS = '31536000'
    SFR_ALLOW_RENDER_TARGETS = '1'; SFR_TRACE_GRAPHICS = '0'; SFR_DIAGNOSTIC_ENTRIES = '0'; SFR_TRACE_IMPORTS = '0'
    SFR_FRAME_LIMIT = $(if ($Capped) { '60' } else { '0' })
    SFR_RENDER_EVERY = '1'; SFR_PARALLEL_WORKER = 'cores'; SFR_VERTEX_CACHE = '1'; SFR_GPU_PIPELINE = '1'
    SFR_AUDIO = '0'; SFR_PROFILE = '1'; SFR_SKIP_MOVIES = '1'
    SFR_WINDOW_WIDTH = '1280'; SFR_WINDOW_HEIGHT = '720'; SFR_FULLSCREEN = '0'; SFR_VSYNC = '0'
    SFR_NUI_HAND_CENTRED = '1'; SFR_SAY = $Say; SFR_PRESENT_LIMIT = "$PresentLimit"
}
# Left unset, whatever this console has: the Kinect, the voice, a second
# player, the per-setting switches and the investigation aids.
$cleared = @('SFR_CAMERA', 'SFR_VOICE', 'SFR_INPUT_SCRIPT', 'SFR_INPUT_SCRIPT_2', 'SFR_GRAPHICS', 'SFR_HOST_PROCESSORS',
             'SFR_SKIP_DRAWS', 'SFR_SCREENSHOT', 'SFR_SCREENSHOT_EVERY', 'SFR_SAMPLE_PROFILE', 'SFR_HOST_PROFILE',
             'SFR_MAIN_PROFILE', 'SFR_AVATAR_MODEL', 'SFR_TWO_PLAYERS', 'SFR_PLAYER2_INPUT')
# Every setting's switch too, so one run's never carries into the next.
$cleared = @($cleared + @($settings.Values | ForEach-Object { $_.Keys }) | Sort-Object -Unique)
$touched = @($base.Keys) + $cleared
$saved = @{}
foreach ($name in ($touched | Sort-Object -Unique)) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }

if (-not $Out) { $Out = Join-Path $root ('out/bench/' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$save_source = Join-Path $host_dir 'save'

# What the numbers belong to.
$cpu = try { (Get-CimInstance Win32_Processor | Select-Object -First 1).Name } catch { 'unknown' }
$gpu = try { (Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name) -join '; ' } catch { 'unknown' }
$commit = (& git -C $root rev-parse --short HEAD 2>$null)
@("commit=$commit", "cpu=$cpu", "gpu=$gpu", "os=$([Environment]::OSVersion.VersionString)",
  "configs=$($Configs -join ',') repeats=$Repeats present_limit=$PresentLimit capped=$([bool]$Capped)",
  "say=$Say") | Set-Content -LiteralPath (Join-Path $Out 'info.txt') -Encoding UTF8

$runs = New-Object System.Collections.Generic.List[object]
if (-not $NoWarmup) { $runs.Add(@('warmup', 1)) }
for ($r = 1; $r -le $Repeats; ++$r) { foreach ($name in $Configs) { $runs.Add(@($name, $r)) } }

function Start-Run([string]$name, [int]$repeat) {
    foreach ($key in $cleared) { Remove-Item "Env:$key" -ErrorAction SilentlyContinue }
    foreach ($key in $base.Keys) { Set-Item "Env:$key" $base[$key] }
    $changes = if ($name -eq 'warmup') { @{} } else { $settings[$name] }
    foreach ($key in $changes.Keys) { Set-Item "Env:$key" $changes[$key] }
    $label = "$name-$repeat"
    # A fresh copy of the save each run: the same menus every time, and the
    # player's own save is never written.
    $save = Join-Path $Out "save-$label"
    if (Test-Path -LiteralPath $save_source) { Copy-Item -Recurse -LiteralPath $save_source -Destination $save }
    else { New-Item -ItemType Directory -Force -Path $save | Out-Null }
    $env:SFR_SAVE_DIRECTORY = $save
    # The warm-up shows where the menus went, should a run not reach the race.
    if ($name -eq 'warmup') {
        $env:SFR_SCREENSHOT = Join-Path $Out 'warmup-%d.bmp'
        $env:SFR_SCREENSHOT_EVERY = '1500'
    }
    $log = Join-Path $Out "$label.log"
    $arguments = @("`"$image`"", "`"$assets`"", '--game-region=ntsc-us')
    return Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -NoNewWindow -PassThru `
        -RedirectStandardError $log -RedirectStandardOutput (Join-Path $Out "$label.out")
}

try {
    $started = Get-Date
    $index = 0
    foreach ($run in $runs) {
        ++$index
        $name, $repeat = $run
        $elapsed = (Get-Date) - $started
        $eta = if ($index -gt 1) { ' (left: about ' + [int](($elapsed.TotalMinutes / ($index - 1)) * ($runs.Count - $index + 1)) + ' min)' } else { '' }
        Write-Output ("[{0}/{1}] {2}, run {3}{4}" -f $index, $runs.Count, $name, $repeat, $eta)
        $process = Start-Run $name $repeat
        if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
            Write-Output "  took over $TimeoutMinutes minutes: stopped"
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
        Remove-Item -Recurse -Force -LiteralPath (Join-Path $Out "save-$name-$repeat") -ErrorAction SilentlyContinue
    }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}

# The py launcher first: 'python' may be the Microsoft Store's stand-in,
# which runs nothing.
$summary = Join-Path $PSScriptRoot 'benchmark_summary.py'
foreach ($python in @(@('py', '-3'), @('python'))) {
    if (-not (Get-Command $python[0] -ErrorAction SilentlyContinue)) { continue }
    $rest = @($python | Select-Object -Skip 1)
    & $python[0] @rest $summary $Out --skip $Skip
    if (Test-Path -LiteralPath (Join-Path $Out 'summary.md')) { break }
}
if (Test-Path -LiteralPath (Join-Path $Out 'summary.md')) { Write-Output "Logs and summary.md are in $Out" }
else { Write-Output "No Python ran the summary; the logs are in $Out (python scripts\benchmark_summary.py `"$Out`")" }
