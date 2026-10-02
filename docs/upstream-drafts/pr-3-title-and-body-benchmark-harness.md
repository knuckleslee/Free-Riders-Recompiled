Title: Add an unattended race benchmark with paired, round-by-round comparison

---

Refs #33. This is independent of the other two branches; say if you would rather not carry it.

## What

`scripts/benchmark.ps1` starts the game, says the menu words that reach a Free Race (`SFR_SAY`), races with nobody at the controls and stops itself. The settings in `-Configs` run `-Repeats` times, taking turns. `scripts/benchmark_summary.py` sums up the frames with `racing=1`.

It exists because several comparisons in the handheld notes were not strict A/B or were inside the noise. What it does about that:

- **Paired by round.** Each setting is compared with the baseline of its own round (ratios per round, their median, how many rounds it was faster).
- **The baseline's own spread** is measured, and a difference smaller than it is reported as inside the noise.
- **The order turns** by one place each round. The same executable run twice showed the second run of a round about 2.4% slower in 6 of 6 rounds; a fixed order would charge that to whichever setting comes later. `-FixedOrder` keeps the old behaviour.
- **Two builds** can be compared in the same rounds (`exe-a`, `exe-b`); the script refuses two files with the same SHA-256 and records the hashes in `info.txt`.
- Runs that never reach the race, or end without a stop line, are listed and left out.

Also: `make_benchmark_kit.ps1` (a folder for a PC without a checkout, with `-UpdateOnly`), `anonymize_benchmark.py` and `benchmark_report.py` (strip user names, language, country, profile ids, computer name and screenshots; no file is over 29 MB, a larger result splits into parts; logs are left out of the report), `profile_summary.py`, `run_benchmark.bat`, and `docs/benchmark.md`.

## Changes to the program (three, none changes a frame's work)

- a `racing=` field on the present line, from the title's race flag `[83E52F8C]`;
- `SFR_SAY_MIN_SECONDS` and `SFR_SAY_REFERENCE_FPS` pace the menu words by the wall clock as well as by presents (on a fast PC a script by presents alone speaks before the menus have loaded and the run never reaches the race);
- `SFR_PRESENT_LIMIT_AFTER_SAY` ends a run N presents after the last word.

## Tests

- The Python tests (summary, anonymizer, report, profile summary) pass.
- Linux build passes; 122 of 123 tests, the failure being the same `native_presentation` check that fails on unmodified `main` under a software Vulkan driver.
- PowerShell cannot run here. The scripts ran on three Windows PCs against v0.4.5 in my fork. This trimmed version (settings your build does not have are removed, `exe-a`/`exe-b` added) ran on an i5 against v0.4.6 through the render thread branch. The newest additions (order turning, hash check, `exe-b-again` control) have **not** run on Windows yet.

## Numbers it produced

See #33: three PCs, v0.4.5, plus the render thread on v0.4.6. Baseline spread was 3-8% by PC.

## Limits

One scenario (a Free Race, one course and character), uncapped, Windows only. A fixed per-frame cost is a larger share of a 10 ms frame than of a 30 ms one, and a desktop result says nothing about a handheld.

Generated with [Claude Code](https://claude.com/claude-code)
