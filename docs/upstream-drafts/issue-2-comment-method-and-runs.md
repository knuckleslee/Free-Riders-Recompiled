Method, machines and per-run frame rates for the issue above.

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
