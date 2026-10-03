@echo off
rem Runs the benchmark on this PC and leaves a copy that is safe to send to others.
rem   run_benchmark.bat speed        baseline against the polling it replaced for suspended guests, 6 rounds
rem   run_benchmark.bat pipelines    a new player first race, with and without the prepared pipelines
rem   run_benchmark.bat exe          two builds, sfr_cpu_diagnostic_a.exe against _b.exe, in the same rounds
rem   run_benchmark.bat exe3         three builds, _a, _b and _c (scripts\build_ab.ps1 -Local), in the same rounds
rem   run_benchmark.bat exe4         four builds, _a to _d (scripts\build_ab.ps1 -Local -Loops)
rem   run_benchmark.bat exe-loops    _a, _b and _d (scripts\build_ab.ps1 -Loops): d is b with checkpoints only at loops
rem   run_benchmark.bat pgo          the usual build against the profile-guided _e (scripts\build_pgo.ps1)
rem   run_benchmark.bat ctrl         a control: the same build twice, to see what the method says about no difference
rem   run_benchmark.bat rt           the render thread on (the default) against off, the same build
rem   run_benchmark.bat par          every guest thread in parallel (SFR_PARALLEL_WORKER=all) against the default
rem   run_benchmark.bat stab         many short races with every guest thread in parallel (all, and all without the thread start delay), 10 each
rem   run_benchmark.bat dc           deferred draws and the index cache (the defaults) against each turned off, 8 rounds
rem   run_benchmark.bat prof         where the main thread spends a race (2 runs); send profile.md
rem   run_benchmark.bat core         the main thread's core kept for it alone, with the default and with all, 5 rounds
rem   run_benchmark.bat smoke        one short race with every build and setting, to check each works first; send the -smoke.md
rem   run_benchmark.bat smoke-all    the same check for only the builds in all mode (exe-a-all ...)
rem   run_benchmark.bat exe-all      the builds _a to _d with every guest thread in parallel, 6 rounds
rem   run_benchmark.bat night        all of pgo, stab, exe, core and prof in a row (5 to 6 hours); send the -overview.zip
rem   run_benchmark.bat speed 8      the same with 8 rounds
rem   run_benchmark.bat author       the original's own unconfirmed switches: constant upload reuse, priority, host timing
rem   run_benchmark.bat speed 2 nostretch   the menu words by presents alone (cannot finish on a fast PC)
setlocal
cd /d "%~dp0"
rem An empty src folder lets an older benchmark.ps1 skip its check of the sources
if not exist "src" mkdir "src"
set MODE=%1
if "%MODE%"=="" set MODE=speed
set REPEATS=%2
set CONFIGS=baseline,no-suspend-notify
set EXTRA=
if "%REPEATS%"=="" set REPEATS=6
if /i "%MODE%"=="author" set CONFIGS=baseline,constant-reuse,priority,no-host-timing
if /i "%MODE%"=="exe" set CONFIGS=exe-a,exe-b
if /i "%MODE%"=="exe3" set CONFIGS=exe-a,exe-b,exe-c
if /i "%MODE%"=="exe4" set CONFIGS=exe-a,exe-b,exe-c,exe-d
if /i "%MODE%"=="exe-all" (
    set CONFIGS=exe-a-all,exe-b-all,exe-c-all,exe-d-all
    set EXTRA=-TimeoutMinutes 10
)
if /i "%MODE%"=="exe-loops" set CONFIGS=exe-a,exe-b,exe-d
if /i "%MODE%"=="pgo" set CONFIGS=baseline,exe-e
if /i "%MODE%"=="rt" set CONFIGS=baseline,no-render-thread
rem A run that stops on an error dialog is ended after 10 minutes, not 25
if /i "%MODE%"=="prof" (
    set CONFIGS=profile
    if "%2"=="" set REPEATS=2
)
if /i "%MODE%"=="dc" (
    set CONFIGS=baseline,no-deferred-draws,no-deferred-constants,no-index-cache
    if "%2"=="" set REPEATS=8
)
if /i "%MODE%"=="stab" (
    set CONFIGS=all,all-stress
    set EXTRA=-AfterSay 1500 -TimeoutMinutes 10
    if "%2"=="" set REPEATS=10
)
if /i "%MODE%"=="par" (
    set CONFIGS=baseline,all
    set EXTRA=-TimeoutMinutes 10
)
if /i "%MODE%"=="ctrl" set CONFIGS=exe-b,exe-b-again
if /i "%MODE%"=="core" (
    set CONFIGS=baseline,main-core,all,all-main-core
    set EXTRA=-TimeoutMinutes 10
    if "%2"=="" set REPEATS=5
)
if /i "%MODE%"=="pipelines" (
    set CONFIGS=baseline,no-prewarm
    set EXTRA=-ColdPipelines
    if "%2"=="" set REPEATS=4
)
if /i "%3"=="nostretch" set EXTRA=%EXTRA% -NoStretch
if not exist "out\build\host\sfr_cpu_diagnostic.exe" goto noexe
set GAMEARGS=
rem The DXC the renderer links pixel shaders with, when the kit carries one
if exist "%~dp0dxc\dxcompiler.dll" set SFR_DXC_LIBRARY=%~dp0dxc
if exist "out\recomp\image-loader" if exist "private\assets" goto run
if exist "out\build\host\settings.ini" goto run
echo The game folders are not inside this folder.
set /p IMAGE=Folder with the game image (image-loader):
set /p ASSETS=Folder with the game assets:
set GAMEARGS=-ImageDirectory "%IMAGE%" -AssetDirectory "%ASSETS%"
:run
echo.
echo The benchmark plays the game by itself. Do not use this PC and close other programs
echo until it says it is done. Mode %MODE%, %REPEATS% rounds.
echo.
if /i "%MODE%"=="night" goto night
if /i "%MODE%"=="smoke" goto smoke
if /i "%MODE%"=="smoke-all" goto smoke
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\benchmark.ps1" -SkipBuildCheck -Configs %CONFIGS% -Repeats %REPEATS% %EXTRA% %GAMEARGS%
echo.
set LATEST=
for /f "delims=" %%Z in ('dir /b /o-d "out\bench\*-shareable*.zip" 2^>nul') do (
    set LATEST=%%Z
    goto found
)
echo No -shareable.zip was made (Python is probably missing).
echo Send the newest folder in out\bench instead, and it will be prepared on the other PC.
goto done
:found
echo Send this file: out\bench\%LATEST%
if not "%LATEST:-part=%"=="%LATEST%" echo It is one of several parts (each under 29 MB): send ALL the files out\bench\%LATEST:~0,15%-shareable-part*.zip
explorer /select,"%~dp0out\bench\%LATEST%"
goto done
:smoke
set SMOKEARGS=
if /i "%MODE%"=="smoke-all" set SMOKEARGS=-AllOnly
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\smoke.ps1" %SMOKEARGS% %GAMEARGS%
set LATEST=
for /f "delims=" %%Z in ('dir /b /o-d "out\bench\smoke-*-smoke.md" 2^>nul') do (
    set LATEST=%%Z
    goto smokefound
)
echo No smoke.md was made (Python is probably missing); send the newest out\bench\smoke-* -shareable zip instead.
goto done
:smokefound
echo Send this file: out\bench\%LATEST%
explorer /select,"%~dp0out\bench\%LATEST%"
goto done
:night
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\night.ps1" %GAMEARGS%
set LATEST=
for /f "delims=" %%Z in ('dir /b /o-d "out\bench\night-*-overview.zip" 2^>nul') do (
    set LATEST=%%Z
    goto nightfound
)
echo No overview was made; send the newest out\bench\night-* folder's -shareable zips instead.
goto done
:nightfound
echo Send this file first: out\bench\%LATEST%
explorer /select,"%~dp0out\bench\%LATEST%"
goto done
:noexe
echo Missing out\build\host\sfr_cpu_diagnostic.exe - this folder is not a complete benchmark kit.
:done
echo.
pause
