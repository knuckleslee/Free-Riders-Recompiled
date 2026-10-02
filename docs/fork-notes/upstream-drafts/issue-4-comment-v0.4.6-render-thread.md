Thank you for integrating all three (#34, #35, #36) so quickly and for the fixes on top. One more measurement that may fit `docs/render-thread.md`, which quotes v0.4.5 numbers only, and a correction to my small numbers above.

## Render thread on v0.4.6 (i5-3470, Radeon RX 480, D3D12)

Two builds from `main` `17506ad` (v0.4.6), unmodified generated code, same kit, six interleaved rounds:

- **A** = `pr/benchmark-harness`: current `main` plus the benchmark scripts and three small program additions (`racing=` field, menu pacing, end after the last word). No change to a frame's work.
- **B** = `test/render-thread-i5`: A plus the render thread (`pr/render-thread`). On D3D12 it is on by default.
- **B, thread off**: the same file as B with `SFR_RENDER_THREAD=0`.

The two executables are different files (122,201,088 and 122,203,136 bytes). 3468 race frames per run.

| Setting | Mean fps (of the runs) | Draw ms | Main thread permit hold ms | Per-round ratio to A | Median |
| --- | ---: | ---: | ---: | --- | ---: |
| A | 25.5 | 7.7 | 29.5 | - | - |
| B | 28.4 | 5.3 | 26.2 | 1.09 1.07 1.12 1.15 1.15 | **1.12** |
| B, thread off | 25.2 | 7.6 | 29.9 | 0.98 0.98 0.96 1.03 1.01 | 0.98 |

- B is faster than A in 5 of 5 rounds. B with its thread off is indistinguishable from A (inside A's own 4% spread), so the whole difference is the render thread.
- One A run (round 4) never reached the race because the menu script was late; it is left out, so A has five runs.
- The settings ran in a fixed order every round (A, B, B thread off). An earlier run, with the same executable twice by mistake, showed the run that comes second in a round measured 2.4% slower in 6 of 6 rounds. B comes second here, so 12% is, if anything, on the low side.
- Not measured on the Ryzen AI MAX+ 395 on v0.4.6; on v0.4.5 the thread had no effect there. Not run on Vulkan or on a device.

## Correction

The benchmark script ran its settings in the same order every round, baseline first. The same-executable run above shows that costs the later settings about 2.4%. Of the numbers in this issue:

- **Not affected in size:** render thread (about 11% on the i5, now 12% again on v0.4.6), the checkpoint interval (6-8% on three PCs), suspension notification on the Ryzen AI MAX+ 395 (about 10%).
- **Treat as uncertain:** the ones at about 3-4% (store checks skipped, recompiled registers kept in locals, suspension notification on the i5), which may include some of that order effect.

The turning order, the same-file check and the `exe-a` comparison are in the harness you merged. Your own paired checkpoint runs on the i9-14900KF/RTX 4090 (+3% to +6% on D3D12, +3% to +4% on Vulkan) point the same way as my three PCs; I was not able to measure Android, so keeping 32 there is the careful choice. I will repeat the small effects with the turning order only if they matter to you.
