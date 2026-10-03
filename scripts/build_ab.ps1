# Builds the same sources twice and leaves both executables where the
# benchmark's exe-a / exe-b settings look for them:
#   sfr_cpu_diagnostic_a.exe  the generated code as it is (out/recomp/diagnostic)
#   sfr_cpu_diagnostic_b.exe  with SFR_FAST_PATH() at each function's entry
#                             (scripts/fast_guest_access.py, out/recomp/diagnostic-fast)
#   sfr_cpu_diagnostic_c.exe  with -Local: the registers kept in locals first
#                             (scripts/localize_registers.py), then the fast path
#                             (out/recomp/diagnostic-local, out/recomp/diagnostic-fastlocal)
#   sfr_cpu_diagnostic_d.exe  with -Loops: the last of those with the label checkpoints
#                             kept only at loops (scripts/loop_checkpoints.py,
#                             out/recomp/diagnostic-loops), so d against b or c is that alone
#
#   scripts\build_ab.ps1 [-Local] [-Loops]
#   scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit
#   run_benchmark.bat exe            # a against b; with -Local, run_benchmark.bat exe3;
#                                    # with -Loops too, run_benchmark.bat exe4
#
# The plain build is made last, so the usual sfr_cpu_diagnostic.exe stays the
# unmodified one. ab.txt beside them says what each is.
param([int]$Jobs = 4, [switch]$Local, [switch]$Loops)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$plain = Join-Path $root 'out/recomp/diagnostic'
$fast = Join-Path $root 'out/recomp/diagnostic-fast'
$local = Join-Path $root 'out/recomp/diagnostic-local'
$fastlocal = Join-Path $root 'out/recomp/diagnostic-fastlocal'
$loops = Join-Path $root 'out/recomp/diagnostic-loops'
if (-not (Test-Path -LiteralPath (Join-Path $plain 'report.json'))) { throw "Missing $plain (the checked generated code: scripts\generate_diagnostic.py)" }
# Made again every time from the generated code here now; only a folder one of
# these tools made (it leaves its report) is removed.
foreach ($made in @(@($fast, 'fast_report.json'), @($local, 'localize_report.json'), @($fastlocal, 'fast_report.json'), @($loops, 'loops_report.json'))) {
    if (Test-Path -LiteralPath $made[0]) {
        if (-not (Test-Path -LiteralPath (Join-Path $made[0] $made[1]))) { throw "$($made[0]) was not made by this script's tools: move it away." }
        Remove-Item -Recurse -Force -LiteralPath $made[0]
    }
}
$py = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
& $py (Join-Path $PSScriptRoot 'fast_guest_access.py') $plain $fast
if ($LASTEXITCODE -ne 0) { throw 'fast_guest_access.py failed.' }
$variants = @(,@('b', 'out/recomp/diagnostic-fast', 'fast path'))
if ($Local) {
    & $py (Join-Path $PSScriptRoot 'localize_registers.py') $plain $local
    if ($LASTEXITCODE -ne 0) { throw 'localize_registers.py failed.' }
    & $py (Join-Path $PSScriptRoot 'fast_guest_access.py') $local $fastlocal
    if ($LASTEXITCODE -ne 0) { throw 'fast_guest_access.py (on the localized copy) failed.' }
    $variants = @(,@('c', 'out/recomp/diagnostic-fastlocal', 'registers in locals and fast path')) + $variants
}
if ($Loops) {
    # On top of the most changed build so far: d differs from it only there.
    $under = $variants[0]
    & $py (Join-Path $PSScriptRoot 'loop_checkpoints.py') (Join-Path $root $under[1]) $loops
    if ($LASTEXITCODE -ne 0) { throw 'loop_checkpoints.py failed.' }
    $variants = @(,@('d', 'out/recomp/diagnostic-loops', "$($under[2]), checkpoints only at loops")) + $variants
}
# The plain build last, so the usual sfr_cpu_diagnostic.exe is the unmodified one.
$variants += ,@('a', 'out/recomp/diagnostic', 'plain')
$commit = (& git -C $root rev-parse --short HEAD).Trim()
$dirty = if (& git -C $root status --porcelain) { 'DIRTY (uncommitted changes)' } else { 'clean' }
$lines = @("commit=$commit tree=$dirty", "built=$(Get-Date -Format s)")
foreach ($variant in $variants) {
    Write-Output "Building $($variant[2]) from $($variant[1])"
    & (Join-Path $PSScriptRoot 'build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $variant[1]
    if ($LASTEXITCODE -ne 0) { throw "Build of $($variant[2]) failed." }
    $copy = Join-Path $host_dir "sfr_cpu_diagnostic_$($variant[0]).exe"
    Copy-Item -Force -LiteralPath (Join-Path $host_dir 'sfr_cpu_diagnostic.exe') -Destination $copy
    $lines += "exe-$($variant[0])=$($variant[2]) from $($variant[1]) size=$((Get-Item -LiteralPath $copy).Length)"
}
$lines | Set-Content -LiteralPath (Join-Path $host_dir 'ab.txt') -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
Write-Output "Next: scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit, then run_benchmark.bat $(if ($Local -and $Loops) { 'exe4' } elseif ($Local) { 'exe3' } elseif ($Loops) { 'exe-loops' } else { 'exe' }) on the i5"
