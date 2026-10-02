Hi, I have been measuring where a Free Race frame goes on three Windows PCs and put together paired A/B data. It may be useful alongside your handheld work, where your notes say several comparisons were not strict A/B or were inside the noise (for example the suspend notification and host timing).

This issue is data only: no logs, no game files, no shader pack, nothing derived from the game. Every number is a per-run mean over the same scripted race, six interleaved rounds per setting, compared round by round against the same round's baseline.

Short version, on v0.4.5 (not re-measured on v0.4.6):

- **Checkpoint call every 256 entries instead of 32:** faster in every round on all three PCs, about 6-8% each.
- **Suspend notification (yours, already in v0.4.5):** faster in every round on the Ryzen AI MAX+ 395 (about 10%); about 3-4% on the i5.
- **Render thread:** about 11% on the i5 (slowest CPU), no effect on the Ryzen AI MAX+ 395. D3D12 only; never run on Vulkan.
- No effect on these PCs: x86 `stvlx`/`stvrx` fast path, pipeline reuse. About 3-4%: ordinary stores skipping the reservation checks, and keeping the recompiled registers in locals.

The measured build is a fork build, not plain v0.4.5, so please read "Version measured" first.

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

Method, machines, per-run frame rates, limits and the code branches follow in two comments.
