# Render thread

`NativeRenderer::draw` used to record its commands (`setPipeline`, descriptors,
`drawIndexedInstanced`) on the guest's main thread, about 1.5 ms of a race
frame. `NativePresentation::record_async` hands that recording to a thread of
its own.

- The body is copied into a fixed-size slot (256 bytes) of a single-producer,
  single-consumer queue of 4096 slots, together with the viewport, scissor and
  `list_generation` the caller saw. It must be trivially copyable and capture
  values, never references to the caller's locals. Alignment cannot exceed 16 bytes.
- Everything else that touches the open command list (`record`, `clear`,
  `draw_player_model`, `present`, `flush`, beginning a list, the work before a
  submission including `before_submit` callbacks) first waits for the queue to
  empty, so the commands land in the order they were asked for.
- Uploads (texture batches, the constant ring, vertex data) and pipeline
  creation stay on the calling thread. The GPU sees the same commands in the same
  order.
- A failure on the render thread is rethrown by the next `record_async` or
  `drain()` on the calling thread. The failed state remains until `flush()`
  drains the queue, waits for prior GPU submissions (including independent
  texture uploads) and discards the partial
  recording; reporting an error does not allow failed work to resume or submit.

## Switch

`SFR_RENDER_THREAD=0` records on the calling thread as before, `=1` forces the
thread. Unset, it is on for D3D12 and off for every other backend: gameplay
performance comparisons target D3D12. Forced-on Vulkan recording is covered by
the ordering and lifetime tests on the local RTX 4090 and by software Vulkan
in CI; this does not establish a Vulkan gameplay performance benefit.

## Measurements (v0.4.5, D3D12, six interleaved rounds each)

| PC | Render thread off, fps ratio to on | Draw ms per frame, on / off |
| --- | --- | --- |
| i5-3470 / RX 480 | median 0.88 (5 of 5 clean rounds slower) | 5.3 / 7.4 |
| Ryzen AI MAX+ 395 | median 1.00 (no effect) | 1.6 / 1.8 |

These reported v0.4.5 results suggest a benefit on that slower CPU and no clear
effect on the other host. They do not establish a gain or zero overhead on all
devices or newer runtime versions.

## Test

`async_records_keep_the_order_of_everything_asked_for` in
`tests/native_presentation_test.cpp` forces the thread on, then alternates
`clear`, an asynchronous clear and, every other round, another `clear`, reading
the colour back: the last command asked for must be the one the GPU holds.

Additional tests verify that callbacks actually run on the worker, preserve
copied values across 6,000 queued records, and finish pending work during
destruction. Injected callback failures cover both observation through `flush`
and through the next enqueue. A deterministic pending Vulkan-fence query checks
that failed cleanup waits for the previous GPU submission before owners can
release its resources. A separate submission without a presentation frame also
verifies that failed cleanup waits for independent upload work.
Presentation and resolution tests pass on the local
RTX 4090 using both D3D12 and Vulkan; Vulkan remains disabled by default.

## Staged draw constants

With the render thread on, a draw's 2048 constant words (vertex then pixel)
are copied from the device as the guest holds them into a staging slot
(`NativeRenderer::constant_staging`), and the render thread swaps them into
that draw's offset of the upload ring before it records the draw. The guest's
thread no longer byte-swaps them or writes them to write-combined memory.
Slots are handed out and released in order; when every slot still waits, the
renderer flushes. `SFR_DEFERRED_CONSTANTS=0` swaps them on the calling thread
as before; the constant reuse experiment (`SFR_CONSTANT_UPLOAD_REUSE=1`) and a
disabled render thread also keep the old path. Tested by the staged-constants
case of `native_resolution_render_thread`; not yet measured in a race.

## Deferred draws

A draw whose constants are staged is deferred whole: its pipeline state
(shaders, input elements, blend, depth, stencil, cull), shared and loop
constants are copied into the same slot, and the render thread resolves the
pipeline (`NativeRenderer::Impl::resolve_pipeline`, under a lock shared with
`take_pipeline_work` and the manifest) and writes those constants into the
ring before recording. Vertices, indices and the palette are still written by
the guest's thread. `SFR_DEFERRED_DRAWS=0` keeps pipeline resolution on the
guest's thread with the constants still staged. Covered by
`native_resolution_render_thread` (a draw with another pipeline between
others) and `native_resolution_staged_constants`; not yet measured in a race.

## Deferred DEC3N repacks

A draw with DEC3N elements that fills the ring (not the vertex cache) is
copied as the guest holds it into an arena (`NativeRenderer::defer_repack`);
the render thread swaps it and repacks it (`repack_dec3n`) into the draw's
ring block. The copy is made on the guest's thread because the device's fence
stays zero, so the title may rewrite a vertex buffer as soon as the draw
returns. It follows `SFR_DEFERRED_CONSTANTS` (a staged draw); `no-deferred-draws`
keeps it. Covered by the deferred-repack case of
`native_resolution_render_thread` and `native_formats`.

## Index cache

Unleashed Recompiled uploads a vertex or index buffer once, when the title
unlocks it. Vertices here already have `SFR_VERTEX_CACHE`; indices now follow
the same rule (`NativeRenderer::index_cache`): a range of indices unwritten for
a frame is decoded once, without the base vertex, into a buffer of 32-bit
indices with its lowest and highest, and later draws bind that buffer
(`NativeDraw::index_buffer`) with the base vertex location instead of decoding
and copying the indices again. Stores to the range drop the buffer (write
epochs); strips cut by restart and scattered indices are decoded every time.
`SFR_INDEX_CACHE=0` turns it off; it is on only with `SFR_VERTEX_CACHE`.
Present lines count `cached_index_draws`. Covered by the index-cache case of
`native_resolution`; not yet measured in a race.
