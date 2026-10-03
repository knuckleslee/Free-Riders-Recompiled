# Builds the same sources twice and leaves both executables where the
# benchmark's exe-a / exe-b settings look for them:
#   sfr_cpu_diagnostic_a.exe  the generated code as it is (out/recomp/diagnostic)
#   sfr_cpu_diagnostic_b.exe  with SFR_FAST_PATH() at each function's entry
#                             (scripts/fast_guest_access.py, out/recomp/diagnostic-fast)
#
#   scripts\build_ab.ps1
#   scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit
#   run_benchmark.bat exe            # on the PC that measures
#
# The plain build is made last, so the usual sfr_cpu_diagnostic.exe stays the
# unmodified one. ab.txt beside them says what each is.
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$plain = Join-Path $root 'out/recomp/diagnostic'
$fast = Join-Path $root 'out/recomp/diagnostic-fast'
if (-not (Test-Path -LiteralPath (Join-Path $plain 'report.json'))) { throw "Missing $plain (the checked generated code: scripts\generate_diagnostic.py)" }
# Made again every time from the generated code here now; only a folder this
# tool made (it leaves fast_report.json) is removed.
if (Test-Path -LiteralPath $fast) {
    if (-not (Test-Path -LiteralPath (Join-Path $fast 'fast_report.json'))) { throw "$fast was not made by fast_guest_access.py: move it away." }
    Remove-Item -Recurse -Force -LiteralPath $fast
}
$py = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
& $py (Join-Path $PSScriptRoot 'fast_guest_access.py') $plain $fast
if ($LASTEXITCODE -ne 0) { throw 'fast_guest_access.py failed.' }
$commit = (& git -C $root rev-parse --short HEAD).Trim()
$dirty = if (& git -C $root status --porcelain) { 'DIRTY (uncommitted changes)' } else { 'clean' }
$lines = @("commit=$commit tree=$dirty", "built=$(Get-Date -Format s)")
foreach ($variant in @(@('b', 'out/recomp/diagnostic-fast', 'fast path'), @('a', 'out/recomp/diagnostic', 'plain'))) {
    Write-Output "Building $($variant[2]) from $($variant[1])"
    & (Join-Path $PSScriptRoot 'build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $variant[1]
    if ($LASTEXITCODE -ne 0) { throw "Build of $($variant[2]) failed." }
    $copy = Join-Path $host_dir "sfr_cpu_diagnostic_$($variant[0]).exe"
    Copy-Item -Force -LiteralPath (Join-Path $host_dir 'sfr_cpu_diagnostic.exe') -Destination $copy
    $lines += "exe-$($variant[0])=$($variant[2]) from $($variant[1]) size=$((Get-Item -LiteralPath $copy).Length)"
}
$lines | Set-Content -LiteralPath (Join-Path $host_dir 'ab.txt') -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
Write-Output 'Next: scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit, then run_benchmark.bat exe on the i5'
