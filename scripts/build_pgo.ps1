# A profile-guided build of the game, left where the benchmark's exe-e looks for it:
#   1. an instrumented build (out\build\pgo-train), copied to
#      out\build\host\sfr_cpu_diagnostic_train.exe
#   2. training: the benchmark's race with it, -TrainRuns times (nobody at the
#      controls); each run writes out\pgo\sfr-<process id>.profraw
#   3. llvm-profdata merges those into out\pgo\sfr.profdata
#   4. the build optimised with them (out\build\pgo-use), copied to
#      out\build\host\sfr_cpu_diagnostic_e.exe, and pgo.txt beside it
# out\build\host's own build is left as it is. Both builds are complete builds of
# the game, each about as long as the usual one; the instrumented game also runs
# more slowly than usual during training.
#
#   scripts\build_pgo.ps1                      # from the plain generated code: e against a
#   scripts\build_pgo.ps1 -SkipTraining        # step 4 again with the profile already merged
#   scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit
#   run_benchmark.bat pgo                       # on the i5: the usual build against e
#
# The profile belongs to the sources it was recorded from: -DiagnosticDirectory
# must be the same for training and step 4 (another, e.g. out/recomp/diagnostic-fast
# from scripts\build_ab.ps1, trains and builds that one instead).
param([int]$Jobs = 4, [string]$DiagnosticDirectory = 'out/recomp/diagnostic',
      [ValidateRange(1, 10)][int]$TrainRuns = 2, [switch]$SkipTraining)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$host_dir = Join-Path $root 'out/build/host'
$pgo = Join-Path $root 'out/pgo'
$profileData = Join-Path $pgo 'sfr.profdata'
$profdata = Join-Path $env:ProgramFiles 'LLVM\bin\llvm-profdata.exe'
if (-not (Test-Path -LiteralPath $profdata)) { throw "Missing $profdata (it comes with the LLVM installer that has clang-cl)" }
if (-not (Test-Path -LiteralPath (Join-Path $host_dir 'sfr_cpu_diagnostic.exe'))) { throw 'Build the game first (scripts\build_tools.ps1 -Diagnostic): the training runs from out\build\host.' }
if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $root $DiagnosticDirectory) 'report.json'))) { throw "Missing $DiagnosticDirectory (checked generated code)" }

if (-not $SkipTraining) {
    Write-Output 'Step 1/4: instrumented build'
    & (Join-Path $PSScriptRoot 'build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $DiagnosticDirectory -BuildDirectory 'out\build\pgo-train' -PgoGenerate
    if ($LASTEXITCODE) { throw 'Instrumented build failed.' }
    Copy-Item -Force -LiteralPath (Join-Path $root 'out/build/pgo-train/sfr_cpu_diagnostic.exe') -Destination (Join-Path $host_dir 'sfr_cpu_diagnostic_train.exe')

    Write-Output "Step 2/4: training, $TrainRuns race(s); leave the PC alone until it ends"
    New-Item -ItemType Directory -Force -Path $pgo | Out-Null
    Get-ChildItem -LiteralPath $pgo -Filter 'sfr-*.profraw' | Remove-Item -Force
    $env:LLVM_PROFILE_FILE = Join-Path $pgo 'sfr-%p.profraw'
    try {
        & (Join-Path $PSScriptRoot 'benchmark.ps1') -Configs 'pgo-train' -Repeats $TrainRuns -NoWarmup -TimeoutMinutes 40 -Out (Join-Path $pgo ('train-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
    } finally {
        Remove-Item Env:LLVM_PROFILE_FILE -ErrorAction SilentlyContinue
    }
    $raw = @(Get-ChildItem -LiteralPath $pgo -Filter 'sfr-*.profraw')
    if (-not $raw.Count) { throw "No profile was written to $pgo (did the races run? see the benchmark output above)" }

    Write-Output "Step 3/4: merging $($raw.Count) profile(s)"
    & $profdata merge "-output=$profileData" @($raw | ForEach-Object { $_.FullName })
    if ($LASTEXITCODE -ne 0) { throw 'llvm-profdata merge failed.' }
} elseif (-not (Test-Path -LiteralPath $profileData)) {
    throw "-SkipTraining needs $profileData from an earlier training"
}

Write-Output 'Step 4/4: optimised build'
& (Join-Path $PSScriptRoot 'build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $DiagnosticDirectory -BuildDirectory 'out\build\pgo-use' -PgoUse $profileData
if ($LASTEXITCODE) { throw 'Profile-guided build failed.' }
$copy = Join-Path $host_dir 'sfr_cpu_diagnostic_e.exe'
Copy-Item -Force -LiteralPath (Join-Path $root 'out/build/pgo-use/sfr_cpu_diagnostic.exe') -Destination $copy
$commit = (& git -C $root rev-parse --short HEAD).Trim()
$dirty = if (& git -C $root status --porcelain) { 'DIRTY (uncommitted changes)' } else { 'clean' }
$lines = @("commit=$commit tree=$dirty", "built=$(Get-Date -Format s)",
           "exe-e=profile-guided from $DiagnosticDirectory profile=$((Get-Item -LiteralPath $profileData).LastWriteTime.ToString('s')) size=$((Get-Item -LiteralPath $copy).Length)")
$lines | Set-Content -LiteralPath (Join-Path $host_dir 'pgo.txt') -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
Write-Output 'Next: scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit, then run_benchmark.bat pgo on the i5'
