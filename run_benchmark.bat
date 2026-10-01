@echo off
rem Runs the benchmark on this PC and leaves a copy that is safe to send to others.
rem   run_benchmark.bat        3 runs of each setting (about an hour)
rem   run_benchmark.bat 6      6 runs of each setting
setlocal
cd /d "%~dp0"
set REPEATS=%1
if "%REPEATS%"=="" set REPEATS=3
if not exist "out\build\host\sfr_cpu_diagnostic.exe" (
    echo Missing out\build\host\sfr_cpu_diagnostic.exe - copy the whole folder from the PC that built it.
    pause
    exit /b 1
)
echo.
echo The benchmark plays the game by itself. Do not use this PC and close other programs
echo until it says it is done. %REPEATS% runs of each setting.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\benchmark.ps1" -SkipBuildCheck -Configs baseline,no-render-thread,no-prewarm,no-suspend-notify -Repeats %REPEATS%
echo.
set LATEST=
for /f "delims=" %%Z in ('dir /b /o-d "out\bench\*-shareable.zip" 2^>nul') do (
    set LATEST=%%Z
    goto found
)
echo No -shareable.zip was made (Python is probably missing).
echo Send the newest folder in out\bench instead, and it will be prepared on the other PC.
goto done
:found
echo Send this file: out\bench\%LATEST%
explorer /select,"%~dp0out\bench\%LATEST%"
:done
echo.
pause
