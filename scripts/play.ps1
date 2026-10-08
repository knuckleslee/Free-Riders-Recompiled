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
    # Every guest thread runs freely, as in Unleashed and Marathon Recompiled
    # (SFR_PARALLEL_WORKER=all, docs/architecture-migration.md); -Serial runs
    # every guest thread one at a time, as before.
    [switch]$Serial,
    # Vertices that stay the same across frames are kept on the GPU side
    # (SFR_VERTEX_CACHE, docs/performance.md); if geometry ever looks stale,
    # -NoVertexCache turns it off.
    [switch]$NoVertexCache,
    [switch]$Mute,                # no sound (SFR_AUDIO)
    # To feel what a smaller PC gets: the game is limited to this many cores,
    # one logical processor each (4 is a 4-core, 4-thread i5). 0 uses them all.
    [int]$Cores = 0,
    # The main thread left to Windows instead of pinned to the first core
    # (SFR_MAIN_AFFINITY=0). With fewer than six logical processors it is
    # left unpinned anyway (native_thread.h); -Pinned pins it there too.
    [switch]$Unpinned,
    [switch]$Pinned,
    # No 60 fps cap, to see how far above 60 the PC would go (races then run
    # faster than real time).
    [switch]$Uncapped,
    [string]$Region = 'ntsc-us',
    # Started through RenderDoc's renderdoccmd (from RenderDoc's folder, or the
    # path given with -RenderDocCmd): F12 in the game captures a frame, saved
    # in out/renderdoc. Open it in RenderDoc to see a frame's draws.
    [switch]$RenderDoc,
    [string]$RenderDocCmd = '',
    # Borderless full screen from the start (SFR_FULLSCREEN; Alt+Enter toggles in the game).
    [switch]$Fullscreen,
    # Records the race with scripts/pmc-record.ps1 (the CPU's counters, and
    # PresentMon when it is found) while the game runs: run this from an
    # administrator PowerShell, and press Enter there once a race is under way.
    [switch]$Record,
    [int]$RecordSeconds = 60,
    [string]$Log = 'out/play.log' # the runtime's trace output
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'out/build/host/sfr_cpu_diagnostic.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: scripts\build_tools.ps1 -Diagnostic (missing $exe)" }
$launch = "`"$exe`""
if ($RenderDoc) {
    if (-not $RenderDocCmd) {
        $RenderDocCmd = @("$env:ProgramFiles\RenderDoc\renderdoccmd.exe", "${env:ProgramFiles(x86)}\RenderDoc\renderdoccmd.exe") |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
    if (-not $RenderDocCmd -or -not (Test-Path -LiteralPath $RenderDocCmd)) { throw 'renderdoccmd.exe not found: install RenderDoc or pass -RenderDocCmd <path>' }
    $captures = Join-Path $root 'out/renderdoc'
    New-Item -ItemType Directory -Force -Path $captures | Out-Null
    # The game inherits this process's environment, so every setting below reaches it.
    $launch = "`"$RenderDocCmd`" capture -w -d `"$root`" -c `"$(Join-Path $captures 'sfr')`" `"$exe`""
}
foreach ($path in 'out/recomp/image-loader', 'private/assets') {
    if (-not (Test-Path -LiteralPath (Join-Path $root $path))) { throw "Missing $path (see README)" }
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
$env:SFR_PARALLEL_WORKER = if ($Serial) { '0' } else { 'all' }
# A race steps a sixtieth of a second per frame: never faster than 60 fps.
$env:SFR_FRAME_LIMIT = if ($Uncapped) { '0' } else { '60' }
if ($Unpinned) { $env:SFR_MAIN_AFFINITY = '0' } elseif ($Pinned) { $env:SFR_MAIN_AFFINITY = '1' } else { Remove-Item Env:SFR_MAIN_AFFINITY -ErrorAction SilentlyContinue }
# One line a frame in the log, for the race's frame rate printed at the end
$env:SFR_FRAME_METRICS = '1'
$env:SFR_VERTEX_CACHE = if ($NoVertexCache) { '0' } else { '1' }
$env:SFR_AUDIO = if ($Mute) { '0' } else { '1' }
# Signed in, so the game keeps records in save/ (docs/saves.md).
$env:SFR_PROFILE = '1'
if ($SkipMovies) { $env:SFR_SKIP_MOVIES = '1' } else { Remove-Item Env:SFR_SKIP_MOVIES -ErrorAction SilentlyContinue }
if ($Fullscreen) { $env:SFR_FULLSCREEN = '1' } else { Remove-Item Env:SFR_FULLSCREEN -ErrorAction SilentlyContinue }
if ($Record) {
    if ($RenderDoc) { throw '-Record and -RenderDoc together would measure RenderDoc too: use one at a time' }
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) { throw '-Record needs an administrator PowerShell (the CPU counters are a kernel trace)' }
}
# The process's processor limit is inherited by cmd and the game, which
# places its threads within it. Logical processors 2k and 2k+1 are taken to
# share a core when there are more of them than cores.
$self = [System.Diagnostics.Process]::GetCurrentProcess()
$allProcessors = $self.ProcessorAffinity
if ($Cores -gt 0) {
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    if ($Cores -gt $cpu.NumberOfCores) { throw "This PC has $($cpu.NumberOfCores) cores" }
    $step = if ($cpu.NumberOfLogicalProcessors -gt $cpu.NumberOfCores) { [int]($cpu.NumberOfLogicalProcessors / $cpu.NumberOfCores) } else { 1 }
    $mask = [int64]0
    for ($i = 0; $i -lt $Cores; $i++) { $mask = $mask -bor ([int64]1 -shl ($i * $step)) }
    $self.ProcessorAffinity = [IntPtr]$mask
}
Push-Location $root
try {
    $limits = @()
    if ($Cores -gt 0) { $limits += "$Cores cores (mask 0x$('{0:X}' -f $mask))" }
    if ($Unpinned) { $limits += 'main thread unpinned' }
    if ($Pinned) { $limits += 'main thread pinned' }
    if ($Uncapped) { $limits += 'no fps cap' }
    if ($RenderDoc) { $limits += 'through RenderDoc (F12 captures a frame into out/renderdoc)' }
    if ($Fullscreen) { $limits += 'full screen' }
    if ($limits.Count) { Write-Output ('Playing with ' + ($limits -join ', ')) }
    Write-Output "Running; trace output goes to $Log. Close the game window or press Ctrl+C here to stop."
    # cmd redirects the trace: PowerShell 5.1 would turn each stderr line into an error record.
    $command = "$launch out/recomp/image-loader private/assets --game-region=$Region 2> `"$Log`""
    if ($Record) {
        # The game in a process of its own, so that this window can record it.
        $game = Start-Process -FilePath $env:COMSPEC -ArgumentList '/d', '/c', "`"$command`"" -PassThru -WindowStyle Hidden
        $waited = 0
        while (-not (Get-Process -Name sfr_cpu_diagnostic -ErrorAction SilentlyContinue) -and -not $game.HasExited -and $waited -lt 120) {
            Start-Sleep -Seconds 1; ++$waited
        }
        if (-not $game.HasExited) {
            Write-Output 'Get into a race, then come back here and press Enter.'
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'pmc-record.ps1') -Process sfr_cpu_diagnostic.exe -Seconds $RecordSeconds -OutDir (Join-Path $root 'out/pmc')
        }
        $game.WaitForExit()
        if ($game.ExitCode -ne 0) { Write-Output "Stopped (exit $($game.ExitCode)); the last lines of $Log say why." }
    } else {
        & $env:COMSPEC /d /c $command
        if ($LASTEXITCODE -ne 0) { Write-Output "Stopped (exit $LASTEXITCODE); the last lines of $Log say why." }
    }
    # The races played, as the benchmark counts them: frames with racing=1, less
    # the first 300 of each race (the countdown).
    $times = [System.Collections.Generic.List[double]]::new()
    $inRace = 0
    $order = ''
    $logPath = if ([System.IO.Path]::IsPathRooted($Log)) { $Log } else { Join-Path $root $Log }
    $lines = if (Test-Path -LiteralPath $logPath) { [System.IO.File]::ReadLines($logPath) } else { @() }
    foreach ($line in $lines) {
        if ($line.StartsWith('NATIVE_HOST_PROCESSORS')) { $order = $line }
        if (-not $line.StartsWith('NATIVE_PRESENT')) { continue }
        if ($line.Contains(' racing=1')) {
            $inRace++
            if ($inRace -gt 300 -and $line -match ' frame_ms=([0-9.]+)') { $times.Add([double]$Matches[1]) }
        } else { $inRace = 0 }
    }
    if ($order) { Write-Output "Host processors: $order" }
    if ($times.Count) {
        $mean = ($times | Measure-Object -Average).Average
        $slow = @($times | Where-Object { $_ -gt 50 }).Count
        Write-Output ("Races: {0} frames, {1:N1} ms a frame ({2:N1} fps), {3} frames over 50 ms" -f $times.Count, $mean, (1000 / $mean), $slow)
    } else { Write-Output 'No race was played long enough to count.' }
} finally {
    Pop-Location
    $self.ProcessorAffinity = $allProcessors
}
