# Starts Sonic Free Riders in the native runtime for play: no diagnostic
# time or call limits. Build first with
# scripts/build_tools.ps1 -Diagnostic -DiagnosticDirectory out/recomp/diagnostic.
# Pad: START starts, A selects, B goes back, the D-pad moves (docs/pad-menus.md).
param(
    [switch]$SkipMovies,          # end every movie after its first frames
    # Races step the game a sixtieth of a second per frame, so below 60 fps
    # they run slow. N > 1 draws only every Nth frame of a race, which lets
    # the game run faster while the screen updates less often (menus are
    # unaffected). A race holds about 60 fps drawing every frame now, so 1
    # is the setting to use.
    [int]$RaceRenderEvery = 1,
    # Guest threads on processors 1-5 run in parallel with the main thread,
    # those the title pinned to one processor taking turns as on the console
    # (SFR_PARALLEL_WORKER=cores, docs/performance.md); -Serial runs every
    # guest thread one at a time, as before.
    [switch]$Serial,
    # -All runs every guest thread in parallel (SFR_PARALLEL_WORKER=all), the
    # mode the fastest build was measured in; experimental: if the game ever
    # stops or hangs with it, play without it.
    [switch]$All,
    # Another build in out/build/host by its letter: -Build f plays
    # sfr_cpu_diagnostic_f.exe (scripts/build_pgo.ps1 -Letter f).
    [ValidatePattern('^[a-z]?$')][string]$Build = '',
    # The game folders, when they are not inside this folder (as for the benchmark)
    [string]$ImageDirectory = '',
    [string]$AssetDirectory = '',
    # Vertices that stay the same across frames are kept on the GPU side
    # (SFR_VERTEX_CACHE, docs/performance.md); if geometry ever looks stale,
    # -NoVertexCache turns it off.
    [switch]$NoVertexCache,
    [switch]$Mute,                # no sound (SFR_AUDIO)
    [string]$Region = 'ntsc-us',
    [string]$Log = 'out/play.log' # the runtime's trace output
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$exe = Join-Path $host_dir $(if ($Build) { "sfr_cpu_diagnostic_$Build.exe" } else { 'sfr_cpu_diagnostic.exe' })
if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: scripts\build_tools.ps1 -Diagnostic (missing $exe)" }
# Where the launcher found the game, else where a checkout keeps it (as benchmark.ps1 does).
$image = Join-Path $root 'out/recomp/image-loader'
$assets = Join-Path $root 'private/assets'
$ini = Join-Path $host_dir 'settings.ini'
if (Test-Path -LiteralPath $ini) {
    foreach ($line in Get-Content -LiteralPath $ini -Encoding UTF8) {
        if ($line -match '^image_directory=(.+)$') { $image = $Matches[1] }
        if ($line -match '^asset_directory=(.+)$') { $assets = $Matches[1] }
    }
}
if ($ImageDirectory) { $image = $ImageDirectory }
if ($AssetDirectory) { $assets = $AssetDirectory }
if (-not (Test-Path -LiteralPath $image)) { $image = Join-Path $root 'out/recomp/image-loader' }
if (-not (Test-Path -LiteralPath $assets)) { $assets = Join-Path $root 'private/assets' }
foreach ($path in $image, $assets) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path (see README)" }
}
# A benchmark kit keeps its own DXC next to the scripts
if (-not $env:SFR_DXC_LIBRARY -and (Test-Path -LiteralPath (Join-Path $root 'dxc\dxcompiler.dll'))) { $env:SFR_DXC_LIBRARY = Join-Path $root 'dxc' }
$env:SFR_CALL_BUDGET = '18446744073709551615'
$env:SFR_WATCHDOG_SECONDS = '31536000'
# Races render through the title's own surfaces; the native backend aliases
# them to its framebuffer (docs/race-scene-rendering.md).
$env:SFR_ALLOW_RENDER_TARGETS = '1'
# The per-call graphics traces are about five thousand lines a frame; playing
# does not audit them and they cost frame time.
$env:SFR_TRACE_GRAPHICS = '0'
# Likewise the per-entry diagnostics: playing does not read the ORIGINAL_*
# audits or the entry traces, and every guest function entry pays for them.
$env:SFR_DIAGNOSTIC_ENTRIES = '0'
$env:SFR_TRACE_IMPORTS = '0'
$env:SFR_RENDER_EVERY = "$RaceRenderEvery"
$env:SFR_PARALLEL_WORKER = if ($Serial) { '0' } elseif ($All) { 'all' } else { 'cores' }
# A race steps a sixtieth of a second per frame: never faster than 60 fps.
$env:SFR_FRAME_LIMIT = '60'
$env:SFR_VERTEX_CACHE = if ($NoVertexCache) { '0' } else { '1' }
$env:SFR_AUDIO = if ($Mute) { '0' } else { '1' }
# Signed in, so the game keeps records in save/ (docs/saves.md).
$env:SFR_PROFILE = '1'
if ($SkipMovies) { $env:SFR_SKIP_MOVIES = '1' } else { Remove-Item Env:SFR_SKIP_MOVIES -ErrorAction SilentlyContinue }
Push-Location $root
try {
    Write-Output "$(Split-Path -Leaf $exe), guest threads: $env:SFR_PARALLEL_WORKER"
    Write-Output "Running; trace output goes to $Log. Close the game window or press Ctrl+C here to stop."
    # cmd redirects the trace: PowerShell 5.1 would turn each stderr line into an error record.
    & $env:COMSPEC /d /c "`"$exe`" `"$image`" `"$assets`" --game-region=$Region 2> `"$Log`""
    if ($LASTEXITCODE -ne 0) { Write-Output "Stopped (exit $LASTEXITCODE); the last lines of $Log say why." }
} finally {
    Pop-Location
}
