# Paired A/B performance data on v0.4.5 (three Windows PCs)

Draft for the original repository's maintainer. It reports measurements only: no
logs, no game files, no shader pack, no pipeline lists and nothing derived from
the game. Every number is a per-run mean over the same scripted race.

## Version measured

| | |
| --- | --- |
| Base | upstream **v0.4.5**, `2ecaa7f` (2026-10-01) |
| Tested build | fork `knuckleslee/Free-Riders-Recompiled`, branch `claude/upstream-kinect`, executable built from commit `1890c0f`; C++ sources identical to `4f2a888` (12 files, +540 / -98 against v0.4.5) |
| Not covered | v0.4.6 and later (`17506ad`, including targeted permit wakeups, batched texture uploads, entry-overhead changes). Nothing below was re-measured on them. |
| Graphics | D3D12 only, 1280x720, vsync off, frame limit off, audio off |

The tested build is **not** v0.4.5 plus one change. Every switch below is on in
the baseline and is turned off by an environment variable for its comparison, so
each A/B changes one thing. What differs from v0.4.5 in the baseline:

| Change | Switch that undoes it |
| --- | --- |
| Draw commands recorded on a render thread | `SFR_RENDER_THREAD=0` |
| Checkpoint call into the permit every 256 entries (v0.4.5: 32) | `SFR_CHECKPOINT_INTERVAL=32` |
| Ordinary stores skip the reservation and pinned-page checks; reserved loads and conditional stores skip the layout search on fast pages | `SFR_STRICT_MEMORY=1` |
| `stvlx`/`stvrx` take the fast page path on x86 (v0.4.5 has it on AArch64 only) | `SFR_FAST_PARTIAL_STORES=0` |
| Repeated draws reuse the previous draw's pipeline when the key matches | `SFR_PIPELINE_REUSE=0` |
| Wake only the next waiting guest (v0.4.6 has its own version of this) | none |
| Registers of the recompiled code kept in per-function locals (post-processor `scripts/localize_registers.py` on the generated code; 28,609 of 30,726 functions) | a second executable built from the unmodified generated code |
| Benchmark support: `racing=` field on the present line, `SFR_SAY` pacing, `SFR_PRESENT_LIMIT_AFTER_SAY` | none (no effect on timing) |

**All baselines below used the register-localized executable**, because it was
the one built last. The only unlocalized runs are the `plain` rows of the first
table. Absolute frame rates are therefore about 4% above what an unlocalized
build gives, and the switch effects are measured against that baseline.

## Method

- One unattended Free Race from the menus to the finish, driven by `SFR_SAY`, nobody at the controls, save copied per run. The first 600 race frames are dropped; **3468 race frames** are measured per run.
- Settings alternate within each round (`baseline`, then each variant) for 6 rounds, after one warm-up run that is not counted. The comparison used is the per-round ratio (variant fps over the same round's baseline fps), because machine state drifts between rounds.
- A result is called real only if the ratio points the same way in (nearly) all rounds and its size is clearly above the baseline's own spread (max-min over the median of the six baseline runs: i5 4-5%, i7 4%, Z13 8%).
- The scripts that do this (`scripts/benchmark.ps1`, `scripts/benchmark_summary.py`, the anonymized report tool) are in the fork and can be offered as a separate contribution.

## Machines

| | CPU | GPU / driver | OS |
| --- | --- | --- | --- |
| i5 | Core i5-3470 | Radeon RX 480, 31.0.21912.14 | Windows 10 19045 |
| i7 | Core i7-6850K | GeForce RTX 3080 Ti in an external (eGPU) enclosure, 32.0.15.8157 | Windows 10 19045 |
| Z13 | Ryzen AI MAX+ 395 (16 cores) | Radeon 8060S integrated, 32.0.31032.1003 | Windows 11 26200, on mains, Turbo power plan, 25% background CPU before the run |

## Results

Ratio = variant fps / baseline fps of the same round. "Faster rounds" = rounds where the variant beat baseline. Frame rates are means over the 3468 frames.

| Machine | Variant (one switch turned back) | Baseline fps | Variant fps | Per-round ratios | Faster rounds |
| --- | --- | ---: | ---: | --- | ---: |
| i5 | render thread off | 30.6 | 27.3 | 0.92 **1.01** 0.88 0.88 0.90 0.88 | 1/6 |
| i7 | checkpoint interval 32 | 37.5 | 34.9 | 0.91 0.99 0.93 0.94 0.91 0.97 | 0/6 |
| i5 | checkpoint interval 32 | 30.2 | 28.6 | 0.97 0.99 0.93 0.92 0.94 0.94 | 0/6 |
| Z13 | checkpoint interval 32 | 106.2 | 97.8 | 0.93 0.95 0.91 0.96 0.89 0.92 | 0/6 |
| Z13 | suspend notification off (`SFR_SUSPEND_NOTIFY=0`) | 106.2 | 95.8 | 0.90 0.95 0.91 0.92 0.88 0.87 | 0/6 |
| i5 | suspend notification off | 30.6 | 29.6 | 0.96 **1.09** 0.97 1.00 0.94 0.94 | 2/6 |
| i5 | strict stores (`SFR_STRICT_MEMORY=1`) | 30.6 | 29.9 | 0.97 **1.08** 0.97 0.97 0.98 0.97 | 1/6 |
| Z13 | render thread off | 106.2 | 104.8 | 1.01 1.01 1.01 0.98 0.99 0.99 | 3/6 |
| i5 | x86 `stvlx`/`stvrx` fast path off | 30.2 | 30.7 | 1.02 1.01 1.03 0.99 1.04 1.02 | 5/6 |
| i5 | pipeline reuse off | 30.2 | 30.2 | 1.01 1.00 0.99 0.97 1.00 1.02 | 3/6 |
| i5 | unlocalized generated code (`plain`) vs localized | 29.2 | 30.4 (localized) | 1.07 1.02 1.05 1.04 1.07 0.99 (localized/plain) | 5/6 localized |

The three bold ratios are rounds 2 of the i5 `fast` run. The i5 baseline of that
round is an outlier (27.9 fps, P99 105 ms, present time 1.9 ms against 0.3 ms in
every other run), which lifts all three ratios of that round. Without that round:

| i5 variant | Ratios without round 2 | Median |
| --- | --- | ---: |
| render thread off | 0.92 0.88 0.88 0.90 0.88 | 0.88 |
| suspend notification off | 0.96 0.97 1.00 0.94 0.94 | 0.96 |
| strict stores | 0.97 0.97 0.97 0.98 0.97 | 0.97 |

### Reading the table

- **Checkpoint interval 256 against 32:** every round is faster on all three machines, 6-8% on each. Largest effect that is the same on every machine.
- **Suspend notification (already in v0.4.5):** faster in every round on Z13 (about 10%); on i5 the effect is small (about 3-4%, four of five clean rounds). Your own note called its fps effect unestablished; this is a paired answer for it.
- **Render thread:** helps the slowest CPU (i5, about 11%: draw time per frame 5.3 ms against 7.4 ms with it off, main-thread permit hold 24.7 against 27.8 ms). No effect on Z13 (ratios 1.00). The i7 was not re-run on this build; an earlier run on an older base also showed no effect. It was only ever executed on D3D12. The Vulkan path was only compiled; the queue's sleep and wake logic had a host-only stress test, which is not a GPU run.
- **Strict stores:** about 3% on i5, in line with an earlier single-machine measurement on the i7.
- **No effect (do not pursue on these PCs):** x86 partial vector stores, pipeline reuse. The `stvlx` row reads as slightly faster with the fast path *off*, which is inside the noise.
- **Register-localized generated code:** about 4% on i5 (5 of 6 rounds). Smaller than expected once v0.4.5's `__savegprlr`/`__restgprlr` hooks exist. Not measured on i7 or Z13.

## Per-run frame rates (mean fps of each run, in run order)

| Run set | Setting | fps |
| --- | --- | --- |
| i5 fast | baseline | 30.5 27.9 30.7 30.8 30.6 30.7 |
| i5 fast | render thread off | 28.1 28.2 27.2 27.0 27.5 27.0 |
| i5 fast | suspend notification off | 29.2 30.4 29.9 30.9 28.8 28.8 |
| i5 fast | strict stores | 29.6 30.2 29.7 29.9 30.0 29.9 |
| i5 paths | baseline | 30.2 29.9 30.5 31.0 30.2 29.7 |
| i5 paths | stvlx/stvrx fast path off | 30.8 30.3 31.3 30.7 31.5 30.2 |
| i5 paths | pipeline reuse off | 30.5 30.0 30.2 30.2 30.1 30.4 |
| i5 paths | checkpoint interval 32 | 29.5 29.7 28.2 28.7 28.5 28.0 |
| i5 ab | unlocalized | 28.6 29.7 28.9 29.6 28.4 30.2 |
| i5 ab | localized | 30.7 30.2 30.4 30.7 30.4 29.8 |
| i7 | baseline | 38.0 36.8 37.6 37.2 38.3 37.4 |
| i7 | checkpoint interval 32 | 34.5 36.5 34.9 35.0 34.9 36.4 |
| Z13 | baseline | 101.2 101.3 108.1 104.5 109.2 107.9 |
| Z13 | render thread off | 102.1 102.4 109.2 101.9 108.5 107.2 |
| Z13 | suspend notification off | 91.2 95.8 98.6 95.9 95.8 93.7 |
| Z13 | checkpoint interval 32 | 94.4 96.7 98.9 100.0 96.7 99.5 |

## Limits to keep in mind

- One scenario (a Free Race, one course and character, scripted menus) on three x86 PCs with D3D12. Nothing here speaks for Android, AArch64, Vulkan or the handhelds this project targets.
- The run is uncapped. On Z13 a frame is about 10 ms, so a fixed per-frame cost shows as a larger percentage there than it would at a 60 fps cap.
- Six rounds per setting; the baseline's own spread is 4-8%. Effects of 3% or less are suggestions, not findings.
- The build is v0.4.5 plus the fork's changes, as listed. v0.4.6 changed scheduling, entry overhead and texture uploads, so these numbers should be re-checked there before acting on them.

## Earlier observations on older bases (not re-measured on v0.4.5)

Measured on i7 around v0.4.0-v0.4.3, listed because they are cheap negative results:

- Waking only the next waiting guest did not change the race frame time (hand-off gap stayed 2.6-5.0 ms). The gap was the host scheduling the next guest, not waking too many.
- Letting the main thread spin for its turn at the permit was neutral (-1%).
- A main-thread profile of a race: about 34% of samples in the generated game code, 30-34% outside the executable (waits, driver, system), 14-17% drawing, 6% checked guest-memory accesses; `__savegprlr`/`__restgprlr` about 2.6% before the localized build and 1.3% after.
- A frame's main-thread permit queueing (4-8 ms) split about half between "behind another guest" and "permit free but the main thread not yet running".
