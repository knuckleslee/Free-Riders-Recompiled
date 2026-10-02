# Testing the render thread branch against main on one PC

Build A is the benchmark branch (main plus the benchmark scripts), build B is
this branch (A plus the render thread). Both use the unmodified generated code.

```powershell
git checkout pr/benchmark-harness
scripts\build_tools.ps1 -Diagnostic
copy out\build\host\sfr_cpu_diagnostic.exe out\build\host\sfr_cpu_diagnostic_a.exe
git checkout test/render-thread-i5
scripts\build_tools.ps1 -Diagnostic
copy out\build\host\sfr_cpu_diagnostic.exe out\build\host\sfr_cpu_diagnostic_b.exe
```

Then `run_benchmark.bat rt` (or `scripts\benchmark.ps1 -Configs exe-a,exe-b,b-no-thread -Repeats 6`)
runs, round by round: A, B, and B with `SFR_RENDER_THREAD=0`. B against A is the
branch's effect; B with the thread off should match A, which shows the thread
is the whole difference.
