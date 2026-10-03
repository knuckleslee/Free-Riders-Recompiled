# Times the two generated-code changes from the fork on top of the branch for the
# original repository (pr/guest-fast-path, upstream #38), built in the same worktree
# as scripts/pr_ab.ps1 (.worktrees\pr-ab), so this checkout's sources and builds are
# left alone. Each differs from q (pr_ab.ps1's build of the branch) in one change:
#   out\build\host\sfr_cpu_diagnostic_r.exe   q with the label checkpoints kept only at loops
#                                             (scripts/loop_checkpoints.py on the branch's output)
#   out\build\host\sfr_cpu_diagnostic_s.exe   q with the registers kept in locals
#                                             (scripts/localize_registers.py on the code without
#                                             SFR_FAST_PATH, with the branch's src for its hooks,
#                                             then scripts/fast_guest_access.py)
# and gen.txt beside them. q is built too when it is missing or -RebuildQ is given.
# The generated code comes from this checkout's out\recomp\ppc and recompile.log
# (read only), through the branch's generator, as in pr_ab.ps1.
#
#   git pull
#   powershell -ExecutionPolicy Bypass -File scripts\pr_gen.ps1
#   scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit
#   run_benchmark.bat pr-gen      (on the i5: q, r and s in the same rounds)
param([string]$Repo = '', [int]$Jobs = 4, [string]$Branch = 'pr/guest-fast-path', [switch]$RebuildQ)
$ErrorActionPreference = 'Stop'
if (-not $Repo) { $Repo = Split-Path -Parent $PSScriptRoot }
$Repo = (Resolve-Path -LiteralPath $Repo).Path
$recomp = Join-Path $Repo 'out\recomp'
foreach ($need in 'ppc', 'recompile.log') {
    if (-not (Test-Path -LiteralPath (Join-Path $recomp $need))) { throw "Missing out\recomp\$need in $Repo (is -Repo the game checkout?)" }
}
# The fork's tools, from this checkout (they are not on the branch).
$tools = Join-Path $Repo 'scripts'
foreach ($need in 'loop_checkpoints.py', 'localize_registers.py', 'fast_guest_access.py') {
    if (-not (Test-Path -LiteralPath (Join-Path $tools $need))) { throw "Missing scripts\$need in $Repo (git pull?)" }
}
$py = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
$hostDir = Join-Path $Repo 'out\build\host'
$buildQ = $RebuildQ -or -not (Test-Path -LiteralPath (Join-Path $hostDir 'sfr_cpu_diagnostic_q.exe'))
$steps = if ($buildQ) { 6 } else { 5 }

Write-Output "1/$steps worktree of origin/$Branch"
& git -C $Repo fetch origin $Branch
if ($LASTEXITCODE) { throw "git fetch origin $Branch failed" }
$wt = Join-Path $Repo '.worktrees\pr-ab'
if (Test-Path -LiteralPath $wt) { & git -C $wt checkout -q --detach "origin/$Branch" }
else { & git -C $Repo worktree add --detach $wt "origin/$Branch" }
if ($LASTEXITCODE) { throw 'git worktree failed' }
$commit = (& git -C $Repo rev-parse --short "origin/$Branch~0").Trim()
foreach ($dir in @(Get-ChildItem -LiteralPath (Join-Path $Repo 'tools') -Directory)) {
    $link = Join-Path $wt "tools\$($dir.Name)"
    if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target $dir.FullName | Out-Null }
}
New-Item -ItemType Directory -Force -Path (Join-Path $wt 'out') | Out-Null
foreach ($name in 'generated', 'out\shaders') {
    if ((Test-Path -LiteralPath (Join-Path $Repo $name)) -and -not (Test-Path -LiteralPath (Join-Path $wt $name))) {
        New-Item -ItemType Junction -Path (Join-Path $wt $name) -Target (Join-Path $Repo $name) | Out-Null
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $wt 'out\shaders\basic\shader_cache_data.cpp'))) {
    Write-Warning "No out\shaders\basic\shader_cache_data.cpp in ${Repo}: the builds get the empty shader cache (as this checkout's own build does)."
}

Write-Output "2/$steps generated code with the branch generator"
$fast = Join-Path $wt 'out\recomp\diagnostic'
$plain = Join-Path $wt 'out\recomp\diagnostic-main'
$loops = Join-Path $wt 'out\recomp\diagnostic-loops'
$local = Join-Path $wt 'out\recomp\diagnostic-local'
$fastlocal = Join-Path $wt 'out\recomp\diagnostic-fastlocal'
foreach ($old in $fast, $plain, $loops, $local, $fastlocal) { if (Test-Path -LiteralPath $old) { Remove-Item -Recurse -Force -LiteralPath $old } }
$arguments = @((Join-Path $wt 'scripts\generate_diagnostic.py'), '--input', (Join-Path $recomp 'ppc'),
               '--log', (Join-Path $recomp 'recompile.log'), '--output', $fast)
if (Test-Path -LiteralPath (Join-Path $recomp 'switches.toml')) { $arguments += @('--switches', (Join-Path $recomp 'switches.toml')) }
& $py @arguments | Out-Null
if ($LASTEXITCODE) { throw 'generate_diagnostic.py of the branch failed' }
# Without SFR_FAST_PATH(); it is upstream main's output (pr_ab.ps1): the localizer's input.
$strip = Join-Path $env:TEMP 'pr_gen_strip.py'
@'
import re, shutil, sys
from pathlib import Path
fast, plain = Path(sys.argv[1]), Path(sys.argv[2])
shutil.copytree(fast, plain)
marker = re.compile(r'^\tSFR_FAST_PATH\(\);\r?\n', re.M)
removed = 0
for path in plain.glob('*.cpp'):
    text = path.read_bytes().decode('utf-8')
    text, count = marker.subn('', text)
    if count:
        path.write_bytes(text.encode('utf-8'))
        removed += count
if not removed: sys.exit('no SFR_FAST_PATH in the branch output: is it the right branch?')
print(f'{removed} SFR_FAST_PATH lines removed')
'@ | Set-Content -LiteralPath $strip -Encoding ASCII
& $py $strip $fast $plain
if ($LASTEXITCODE) { throw 'Making the copy without SFR_FAST_PATH failed: nothing was built.' }

Write-Output "3/$steps r: label checkpoints only at loops"
& $py (Join-Path $tools 'loop_checkpoints.py') $fast $loops
if ($LASTEXITCODE) { throw 'loop_checkpoints.py failed: nothing was built.' }
Write-Output "3/$steps s: registers in locals (hooks from the branch's src), then the fast path"
& $py (Join-Path $tools 'localize_registers.py') $plain $local --sources (Join-Path $wt 'src') | Out-Null
if ($LASTEXITCODE) { throw 'localize_registers.py failed: nothing was built.' }
& $py (Join-Path $tools 'fast_guest_access.py') $local $fastlocal
if ($LASTEXITCODE) { throw 'fast_guest_access.py failed: nothing was built.' }

New-Item -ItemType Directory -Force -Path $hostDir | Out-Null
$built = Join-Path $wt 'out\build\host\sfr_cpu_diagnostic.exe'
$lines = @("branch=origin/$Branch commit=$commit built=$(Get-Date -Format s)")
$variants = @(@('r', 'out/recomp/diagnostic-loops', 'q with label checkpoints only at loops'),
              @('s', 'out/recomp/diagnostic-fastlocal', 'q with the registers in locals'))
if ($buildQ) { $variants = @(,@('q', 'out/recomp/diagnostic', "$Branch, its generator's output")) + $variants }
$step = 4
foreach ($variant in $variants) {
    Write-Output "$step/$steps building $($variant[0]): $($variant[2])"
    & (Join-Path $wt 'scripts\build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $variant[1]
    if ($LASTEXITCODE) { throw "Build of $($variant[0]) failed." }
    $copy = Join-Path $hostDir "sfr_cpu_diagnostic_$($variant[0]).exe"
    Copy-Item -Force -LiteralPath $built -Destination $copy
    $lines += "exe-$($variant[0])=$($variant[2]) size=$((Get-Item -LiteralPath $copy).Length)"
    $step++
}
if (-not $buildQ) { $lines += "exe-q=kept from pr_ab.ps1 size=$((Get-Item -LiteralPath (Join-Path $hostDir 'sfr_cpu_diagnostic_q.exe')).Length)" }
$lines | Set-Content -LiteralPath (Join-Path $hostDir 'gen.txt') -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
Write-Output 'Next: scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit, then run_benchmark.bat pr-gen on the i5'
