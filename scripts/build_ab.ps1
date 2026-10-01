# Builds the same sources twice, with the generated code as the game was
# recompiled (plain) and with its registers kept in locals (local), and leaves
# both executables beside the usual one so one benchmark kit can compare them:
#
#   scripts\build_ab.ps1
#   scripts\make_benchmark_kit.ps1 -UpdateOnly     # or the whole kit, as always
#   run_benchmark.bat ab                           # on the PC that measures
#
# Both come from this checkout's commit; ab.txt says which, and benchmark.ps1
# copies it into the results. A tree with uncommitted changes is allowed but
# marked, so a result is never taken for the commit alone.
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$plain = Join-Path $root 'out/recomp/diagnostic'
$local = Join-Path $root 'out/recomp/diagnostic-local'
if (-not (Test-Path -LiteralPath $plain)) { throw "Missing $plain (the generated code: scripts\generate_diagnostic.py)" }
# The localized copy is made again every time, from the generated code that is here
# now (an older one may come from before a merge). The tool refuses to write over a
# folder, so the old one goes first, but only one this tool made (it leaves
# localize_report.json in it).
if (Test-Path -LiteralPath $local) {
    if (-not (Test-Path -LiteralPath (Join-Path $local 'localize_report.json'))) { throw "$local was not made by localize_registers.py: move it away." }
    Remove-Item -Recurse -Force -LiteralPath $local
}
$py = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
& $py (Join-Path $PSScriptRoot 'localize_registers.py') $plain $local
if ($LASTEXITCODE -ne 0) { throw 'localize_registers.py failed.' }
$commit = (& git -C $root rev-parse --short HEAD).Trim()
$dirty = if (& git -C $root status --porcelain) { 'DIRTY (uncommitted changes)' } else { 'clean' }
$lines = @("commit=$commit tree=$dirty", "built=$(Get-Date -Format s)")
foreach ($variant in @(@('plain', 'out/recomp/diagnostic'), @('local', 'out/recomp/diagnostic-local'))) {
    Write-Output "Building $($variant[0]) from $($variant[1])"
    & (Join-Path $PSScriptRoot 'build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $variant[1]
    if ($LASTEXITCODE -ne 0) { throw "Build of $($variant[0]) failed." }
    $built = Join-Path $host_dir 'sfr_cpu_diagnostic.exe'
    $copy = Join-Path $host_dir "sfr_cpu_diagnostic_$($variant[0]).exe"
    Copy-Item -Force -LiteralPath $built -Destination $copy
    $lines += "$($variant[0])=$($variant[1]) size=$((Get-Item -LiteralPath $copy).Length)"
}
$lines | Set-Content -LiteralPath (Join-Path $host_dir 'ab.txt') -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
Write-Output 'Next: scripts\make_benchmark_kit.ps1 -UpdateOnly (the kit gets both executables and ab.txt)'
