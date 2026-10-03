Comment for #33. Not posted; the owner of this fork decides. Fill the two `TODO` sections from
`run_benchmark.bat pr-all` and `pr-stab` (plain v0.4.7, `_p` from `scripts/pr_ab.ps1`) first, and drop
the fork-build section if the plain numbers say the same.

---

Some data on `SFR_PARALLEL_WORKER=all` against the default `cores`, in case it helps decide whether `all` is worth another look now that the work-share wait (`SFR_WORK_SHARE_WAIT_MS`) and the reservation ABA are fixed. This is data, not a request to change the default: I have only run Free Race, and the R6025 you saw was on Grand Prix loading.

## Speed, plain v0.4.7 (i5-3470, RX 480, D3D12)

TODO: `pr-all`, `exe-p` (cores) against `exe-p-all` (all), the same executable built from `main` `e6fc52e` with unchanged generated code. Six interleaved rounds plus a warm-up. fps over race seconds 20 to 75, per-round ratios, faster rounds, the cores build's own spread, main-thread queue and wait per frame.

## Speed on my fork build (v0.4.7 plus render-thread changes)

Same machine, same harness, six interleaved rounds, all 13 runs ended normally. fps over race seconds 20 to 75 (a fixed number of frames favours the faster setting, because it covers more of the light start of the race):

| | fps (6 runs) | P95 ms | main-thread hold ms | queue ms | wait ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| `cores` | 25.5–26.3 | 46.7–49.9 | 27.8–29.1 | 4.2–4.4 | 5.6–5.9 |
| `all` | 29.7–31.2 | 39.6–43.7 | 26.9–28.6 | 2.3–2.4 | 2.7–2.8 |

- Per-round ratios 1.20 1.17 1.20 1.18 1.14 1.13, median **1.18**, 6 of 6. The `cores` runs were within 3% of each other.
- The gain matches the mechanism: the main thread's queue plus wait per frame drops from about 10 ms to about 5 ms. Its own hold time barely changes.
- Draws per frame were about 4% lower with `all` (747–800 against 785–821). That is within how much the race itself varies between runs, but I mention it in case `all` changes what the game draws.

## Stability so far

- Fork builds, `all`: 14 Free Races that each reached the race proper (66–111 s of race) ended normally, including one with `SFR_THREAD_START_DELAY_US=0`, the setting that reproduced the R6025 before. No hang report.
- TODO: `pr-stab`, plain v0.4.7: 10 short races with `all` and 10 with `all` plus `SFR_THREAD_START_DELAY_US=0` and `SFR_HANG_SECONDS=60`. Each loads a track and goes through the work share at `823B5D40`.
- **Not run:** Grand Prix, other modes, Vulkan, Android, the Ryzen AI MAX+ 395 with `all`.
