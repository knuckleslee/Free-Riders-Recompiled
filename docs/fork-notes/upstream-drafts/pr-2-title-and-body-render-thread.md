Title: perf: record draw commands on a render thread (D3D12 by default)

---

Refs #33.

## What

`NativeRenderer::draw` records its commands (`setPipeline`, descriptors, `drawIndexedInstanced`) on the guest's main thread, about 1.5 ms of a race frame. `NativePresentation::record_async` copies the recording into a fixed-size slot of a single-producer, single-consumer queue (4096 slots of 256 bytes) and a render thread makes the calls.

- Everything else that touches the open command list (`record`, `clear`, `draw_player_model`, `present`, `flush`, beginning a list, the work before a submission) first waits for the queue to empty, so the commands land in the order they were asked for. This includes the `before_submit` callbacks from the texture upload batch: the queue drains first, then the callbacks run.
- Uploads, the constant ring and pipeline creation stay on the calling thread. The GPU sees the same commands in the same order.
- A failure on the render thread is rethrown on the calling thread by the next `record_async` or `drain()`.
- `SFR_RENDER_THREAD=0` records on the calling thread as before, `=1` forces the thread. **Unset, it is on for D3D12 only and off for every other backend**, because D3D12 is the only one measured.

Files: `native_presentation.{h,cpp}`, `native_renderer.cpp`, one test, `docs/render-thread.md`.

## Measurements

i5-3470 / RX 480, D3D12, **v0.4.6** (`17506ad`). A = current `main` plus the benchmark scripts, B = A plus this branch, B with `SFR_RENDER_THREAD=0`. Different files (122,201,088 and 122,203,136 bytes). Six rounds, 3468 race frames per run; one A run never reached the race (menu script late) and is left out.

| | Mean fps | Draw ms | Main thread permit hold ms | Per-round ratio to A | Median |
| --- | ---: | ---: | ---: | --- | ---: |
| A | 25.5 | 7.7 | 29.5 | - | - |
| B | 28.4 | 5.3 | 26.2 | 1.09 1.07 1.12 1.15 1.15 | **1.12** |
| B, thread off | 25.2 | 7.6 | 29.9 | 0.98 0.98 0.96 1.03 1.01 | 0.98 |

B is faster in 5 of 5 rounds, and B with its thread off is indistinguishable from A, so the whole difference is the thread. The order was fixed (A, B, B off); an earlier same-executable check had the second run of a round 2.4% slower, so 12% is on the low side. On v0.4.5 the same switch on the same PC gave 0.88 to 0.91 (off over on) in three separate runs.

| PC | Result |
| --- | --- |
| Ryzen AI MAX+ 395, v0.4.5 | no effect (ratio 1.00); draw 1.6 ms against 1.8 ms |
| i7-6850K, v0.4.5, older base | no effect |

It helps a slow CPU and does not hurt a fast one.

## Tests

- New: `async_records_keep_the_order_of_everything_asked_for`, which forces the thread on and alternates `clear`, an asynchronous clear and a readback; the last command asked for must be the one the GPU holds. It passes on a software Vulkan driver (lavapipe) three times in a row.
- Linux build, 122 of 123 tests pass. The failing one, `native_presentation`, fails identically on unmodified `main` under lavapipe (the swap-chain growth check), so the new test only ran with that check skipped locally.

## Not covered

Never run on a real Vulkan or Android device. Not run on Windows against v0.4.6 beyond the i5 above. Not measured on the Ryzen AI MAX+ 395 on v0.4.6.

Generated with [Claude Code](https://claude.com/claude-code)
