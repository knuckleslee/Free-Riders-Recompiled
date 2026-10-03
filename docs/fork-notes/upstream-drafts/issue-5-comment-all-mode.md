Posted as https://github.com/YuutaTsubasa/Free-Riders-Recompiled/issues/33#issuecomment-5971867013 (2026-10-04).

Comment for #33. Not posted; the owner of this fork decides. Complete as it is, from fork builds only.
If `run_benchmark.bat pr-all` / `pr-stab` (plain v0.4.7) are run later, their numbers can replace or join
the fork-build ones.

---

Some data on `SFR_PARALLEL_WORKER=all` against the default `cores`, in case it helps decide whether `all` is worth another look now that the work-share wait (`SFR_WORK_SHARE_WAIT_MS`) and the reservation ABA are fixed. This is data, not a request to change the default: I have only run Free Race, and the R6025 you saw was on Grand Prix loading.

## Speed (i5-3470, RX 480, D3D12)

One executable from my fork (v0.4.7 plus render-thread changes: draw constants staged and draws deferred to the render thread), run with `cores` and with `all`. Benchmark harness, six interleaved rounds, all 13 runs ended normally. fps over race seconds 20 to 75 (a fixed number of frames favours the faster setting, because it covers more of the light start of the race):

| | fps (6 runs) | P95 ms | main-thread hold ms | queue ms | wait ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| `cores` | 25.5–26.3 | 46.7–49.9 | 27.8–29.1 | 4.2–4.4 | 5.6–5.9 |
| `all` | 29.7–31.2 | 39.6–43.7 | 26.9–28.6 | 2.3–2.4 | 2.7–2.8 |

- Per-round ratios 1.20 1.17 1.20 1.18 1.14 1.13, median **1.18**, 6 of 6. The `cores` runs were within 3% of each other.
- The gain matches the mechanism: the main thread's queue plus wait per frame drops from about 10 ms to about 5 ms. Its own hold time barely changes.
- Draws per frame were about 4% lower with `all` (747–800 against 785–821). That is within how much the race itself varies between runs, but I mention it in case `all` changes what the game draws.

## Stability so far

- Fork builds, `all`: 14 Free Races that each reached the race proper (66–111 s of race) ended normally, including one with `SFR_THREAD_START_DELAY_US=0`, the setting that reproduced the R6025 before. No hang report.
- **Not run:** plain v0.4.7 with `all` (the numbers above are from my fork build; the difference between the two settings should not depend much on the render-thread changes, but I have not checked), Grand Prix, other modes, Vulkan, Android, the Ryzen AI MAX+ 395 with `all`.
