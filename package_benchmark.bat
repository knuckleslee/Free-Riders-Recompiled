@echo off
rem Copies this folder, ready to run on another PC (run_benchmark.bat there).
rem   package_benchmark.bat D:\sfr-bench
rem Leaves out git, old results and the Linux build. The copy holds your own copy of
rem the game: keep it on your own PCs.
setlocal
if "%~1"=="" (
    echo Usage: package_benchmark.bat DESTINATION_FOLDER
    pause
    exit /b 1
)
robocopy "%~dp0." "%~1" /E /XD .git out\bench out\build\linux .worktrees /XF *.zip /NFL /NDL /NJH
if errorlevel 8 (
    echo Copy failed.
    pause
    exit /b 1
)
del "%~1\out\build\host\settings.ini" 2>nul
echo.
echo Done. On the other PC, open %~1 and double-click run_benchmark.bat
pause
