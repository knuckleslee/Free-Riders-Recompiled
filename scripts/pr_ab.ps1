# Times the branch for the original repository, pr/guest-fast-path, against plain
# upstream main, both built in a worktree of their own (.worktrees\pr-ab), so this
# checkout's sources and builds (a running build_ab.ps1 or build_pgo.ps1 too) are
# left alone. Only two files are added here at the end:
#   out\build\host\sfr_cpu_diagnostic_p.exe   no fast path: the branch built from code without SFR_FAST_PATH();, which
#                                             takes the member calls of upstream main (the macros' NoFastPath)
#   out\build\host\sfr_cpu_diagnostic_q.exe   pr/guest-fast-path (its own generator's output)
# and pr.txt beside them. The generated code of both comes from this checkout's
# out\recomp\ppc and recompile.log (read only), through the branch's generator: q as
# it writes it, p with its SFR_FAST_PATH(); lines removed (which is what upstream
# main's generator writes), so the two differ in nothing else.
#
# Without pulling this checkout (a build may be running from it):
#   git fetch origin
#   git show origin/claude/upstream-kinect:scripts/pr_ab.ps1 | Out-File -Encoding ascii $env:TEMP\pr_ab.ps1
#   powershell -ExecutionPolicy Bypass -File $env:TEMP\pr_ab.ps1 -Repo C:\Users\Knuckles\Documents\free-riders-recompiled
# Then, with nothing else building: git pull, scripts\make_benchmark_kit.ps1 -UpdateOnly
# -Destination X:\sfr-benchmark-kit, and run_benchmark.bat pr on the i5.
param([string]$Repo = (Get-Location).Path, [int]$Jobs = 4, [string]$Branch = 'pr/guest-fast-path')
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path -LiteralPath $Repo).Path
$recomp = Join-Path $Repo 'out\recomp'
foreach ($need in 'ppc', 'recompile.log') {
    if (-not (Test-Path -LiteralPath (Join-Path $recomp $need))) { throw "Missing out\recomp\$need in $Repo (is -Repo the game checkout?)" }
}
$py = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }

Write-Output "1/5 worktree of origin/$Branch"
& git -C $Repo fetch origin $Branch
if ($LASTEXITCODE) { throw "git fetch origin $Branch failed" }
$wt = Join-Path $Repo '.worktrees\pr-ab'
if (Test-Path -LiteralPath $wt) { & git -C $wt checkout -q --detach "origin/$Branch" }
else { & git -C $Repo worktree add --detach $wt "origin/$Branch" }
if ($LASTEXITCODE) { throw 'git worktree failed' }
$upstream = (& git -C $Repo rev-parse --short "origin/$Branch~0").Trim()
# The fetched tools (XenonRecomp, SDL, ...) are not in git: the worktree reads this checkout's.
foreach ($dir in @(Get-ChildItem -LiteralPath (Join-Path $Repo 'tools') -Directory)) {
    $link = Join-Path $wt "tools\$($dir.Name)"
    if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target $dir.FullName | Out-Null }
}
if ((Test-Path -LiteralPath (Join-Path $Repo 'generated')) -and -not (Test-Path -LiteralPath (Join-Path $wt 'generated'))) {
    New-Item -ItemType Junction -Path (Join-Path $wt 'generated') -Target (Join-Path $Repo 'generated') | Out-Null
}

Write-Output '2/5 generated code with the branch generator'
$fast = Join-Path $wt 'out\recomp\diagnostic'
$plain = Join-Path $wt 'out\recomp\diagnostic-main'
foreach ($old in $fast, $plain) { if (Test-Path -LiteralPath $old) { Remove-Item -Recurse -Force -LiteralPath $old } }
$arguments = @((Join-Path $wt 'scripts\generate_diagnostic.py'), '--input', (Join-Path $recomp 'ppc'),
               '--log', (Join-Path $recomp 'recompile.log'), '--output', $fast)
if (Test-Path -LiteralPath (Join-Path $recomp 'switches.toml')) { $arguments += @('--switches', (Join-Path $recomp 'switches.toml')) }
& $py @arguments | Out-Null
if ($LASTEXITCODE) { throw 'generate_diagnostic.py of the branch failed' }

# Upstream main's generator differs from the branch's only by the SFR_FAST_PATH(); line it
# writes after each prologue, so the branch output without those lines is main's output.
Write-Output '3/5 the same code without SFR_FAST_PATH(); (what upstream main generates)'
$strip = Join-Path $env:TEMP 'pr_ab_strip.py'
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
if ($LASTEXITCODE) { throw 'Making the plain copy failed: nothing was built.' }

$hostDir = Join-Path $Repo 'out\build\host'
New-Item -ItemType Directory -Force -Path $hostDir | Out-Null
$built = Join-Path $wt 'out\build\host\sfr_cpu_diagnostic.exe'
$lines = @("branch=origin/$Branch commit=$upstream built=$(Get-Date -Format s)")
$step = 4
foreach ($variant in @(@('p', 'out/recomp/diagnostic-main', 'the branch without SFR_FAST_PATH lines: upstream main behaviour'),
                       @('q', 'out/recomp/diagnostic', "$Branch, its generator's output"))) {
    Write-Output "$step/5 building $($variant[0]): $($variant[2])"
    & (Join-Path $wt 'scripts\build_tools.ps1') -Jobs $Jobs -Diagnostic -DiagnosticDirectory $variant[1]
    if ($LASTEXITCODE) { throw "Build of $($variant[0]) failed." }
    $copy = Join-Path $hostDir "sfr_cpu_diagnostic_$($variant[0]).exe"
    Copy-Item -Force -LiteralPath $built -Destination $copy
    $lines += "exe-$($variant[0])=$($variant[2]) size=$((Get-Item -LiteralPath $copy).Length)"
    $step++
}
$lines | Set-Content -LiteralPath (Join-Path $hostDir 'pr.txt') -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
Write-Output 'Next, with nothing else building: git pull, scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit, then run_benchmark.bat pr on the i5'
