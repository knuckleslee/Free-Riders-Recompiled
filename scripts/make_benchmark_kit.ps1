# Makes a folder (and a zip) that runs the benchmark on another PC without git,
# Visual Studio or a build: only what the benchmark needs, from this built checkout.
#
#   scripts\make_benchmark_kit.ps1                  # kit without the game
#   scripts\make_benchmark_kit.ps1 -IncludeGame     # with your copy of the game (large)
#
# On the other PC, unzip it and double-click run_benchmark.bat. Without -IncludeGame
# the other PC needs the game's image and asset folders of its own, and the
# batch file asks where they are. The shader pack and the game folders come from
# your copy of the game: keep a kit that holds them on your own PCs.
param(
    [string]$Destination = '',
    [switch]$IncludeGame,
    [switch]$NoZip
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path (Split-Path -Parent $root) 'sfr-benchmark-kit' }
$host_dir = Join-Path $root 'out/build/host'
if (-not (Test-Path -LiteralPath (Join-Path $host_dir 'sfr_cpu_diagnostic.exe'))) {
    throw "Build first: scripts\build_tools.ps1 -Diagnostic (missing sfr_cpu_diagnostic.exe)"
}
if (Test-Path -LiteralPath $Destination) { Remove-Item -Recurse -Force -LiteralPath $Destination }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

function Copy-Into([string]$relative, [string[]]$exclude = @()) {
    $from = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $from)) { return $false }
    $to = Join-Path $Destination $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    if ((Get-Item -LiteralPath $from).PSIsContainer) {
        Copy-Item -Recurse -Force -LiteralPath $from -Destination $to -Exclude $exclude
        if ($exclude.Count) {
            foreach ($pattern in $exclude) { Get-ChildItem -LiteralPath $to -Recurse -Filter $pattern -ErrorAction SilentlyContinue | Remove-Item -Force }
        }
    } else { Copy-Item -Force -LiteralPath $from -Destination $to }
    return $true
}

Copy-Into 'run_benchmark.bat' | Out-Null
try { (& git -C $root rev-parse --short HEAD 2>$null) | Set-Content -LiteralPath (Join-Path $Destination 'commit.txt') -Encoding ASCII } catch { }
foreach ($file in 'benchmark.ps1', 'benchmark_summary.py', 'profile_summary.py', 'anonymize_benchmark.py') {
    Copy-Into "scripts/$file" | Out-Null
}
Copy-Into 'out/build/host' @('*.pdb', '*.ilk', '*.obj', '*.map', 'settings.ini') | Out-Null
if (-not (Copy-Into 'out/shaders')) { Write-Warning 'out\shaders (the shader pack) was not found: the game may translate its shaders at the first start, which needs this checkout.' }
Copy-Into 'data/pipeline-manifests' | Out-Null
# The renderer links the pixel shaders of the D3D12 backend with this DXC, found
# relative to the folder the game runs in (NativeRenderer's DxcLinker).
if (-not (Copy-Into 'tools/XenosRecomp/thirdparty/dxc-bin/bin/x64' @('dxc.exe'))) {
    Write-Warning 'tools\XenosRecomp\thirdparty\dxc-bin\bin\x64 (dxcompiler.dll) was not found: the game stops at its first draw with dxcompiler.dll is unavailable.'
}

if ($IncludeGame) {
    $image = Join-Path $root 'out/recomp/image-loader'
    $assets = Join-Path $root 'private/assets'
    $ini = Join-Path $host_dir 'settings.ini'
    if (Test-Path -LiteralPath $ini) {
        foreach ($line in Get-Content -LiteralPath $ini -Encoding UTF8) {
            if ($line -match '^image_directory=(.+)$') { $image = $Matches[1] }
            if ($line -match '^asset_directory=(.+)$') { $assets = $Matches[1] }
        }
    }
    foreach ($pair in @(@($image, 'out/recomp/image-loader'), @($assets, 'private/assets'))) {
        if (-not (Test-Path -LiteralPath $pair[0])) { throw "Missing $($pair[0])" }
        $to = Join-Path $Destination $pair[1]
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item -Recurse -Force -LiteralPath $pair[0] -Destination $to
    }
}

$size = [math]::Round((Get-ChildItem -Recurse -File -LiteralPath $Destination | Measure-Object Length -Sum).Sum / 1MB)
Write-Output "Kit folder: $Destination ($size MB)"
if (-not $NoZip -and (Get-Command tar -ErrorAction SilentlyContinue)) {
    $zip = "$Destination.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -Force -LiteralPath $zip }
    & tar -a -c -f $zip -C $Destination .
    if (Test-Path -LiteralPath $zip) { Write-Output "Kit zip:    $zip" }
}
Write-Output 'On the other PC: unzip, then double-click run_benchmark.bat'
