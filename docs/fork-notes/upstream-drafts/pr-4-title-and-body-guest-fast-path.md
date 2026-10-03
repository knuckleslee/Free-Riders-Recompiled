Title: perf: take guest memory's fast path once per generated function

---

Refs #33. Branch: [`pr/guest-fast-path`](https://github.com/knuckleslee/Free-Riders-Recompiled/tree/pr/guest-fast-path), one commit (plus a doc fix) on `main` (`e6fc52e`, v0.4.7). 6 files, +180 / -10.

## What

The build has no strict aliasing, so after each guest store in generated code the compiler has to reload `sfr::active_memory`, the fast page table, the pinned-page table and the base before the next access.

- `generate_diagnostic.py` now emits `SFR_FAST_PATH();` after each `PPC_FUNC_PROLOGUE();`. It copies those pointers and the reservation owner into a local `GuestMemory::FastPath`, once per call.
- The `PPC_LOAD_*`/`PPC_STORE_*` macros use that local and the function's own `base` argument.

## Why it should be safe

- **Same decision as the members.** A load is direct only on a `fast_access` page within one page. A store is direct only when, in addition, this thread holds no live reservation and the page is not pinned, and a watched page still records the write. Everything else calls the member `load`/`store` unchanged: `fast_special` pages, page-crossing words, import variables, computed words, unmapped addresses.
- **Nothing that can change is cached.** The cached pointers do not change for the lifetime of a `GuestMemory`. Page flags and pin counts are still read at every access, with the same relaxed atomics.
- **Code without the marker is unaffected.** It resolves `sfr_fast` to a namespace-scope `NoFastPath` and gets the member calls it had before. No hand-written code uses these macros today.
- **It is tested.** `guest_memory_test` compares the fast path with the members on ordinary, page-crossing, computed and variable words, unmapped addresses, watched pages, a live reservation and a pinned page.

## Measurements

The setup:

- **Machine:** i5-3470 with an RX 480, D3D12, Windows 10.
- **Runs:** benchmark harness, `exe-a` against `exe-b`, six interleaved rounds plus one uncounted warm-up. All 13 runs ended normally.
- **Frame rate:** taken over the same stretch of race time (seconds 20 to 75).

| | plain | fast path | change | per-round ratio | faster rounds |
| --- | ---: | ---: | ---: | --- | ---: |
| fps, race seconds 20–75 | 24.9 | 27.5 | **+11%** | 1.19 1.16 1.04 1.17 1.06 1.07 | 6/6 |

- The plain build's own spread was 8%. The median gain was +12%.
- Main-thread permit hold per frame went from 25.8 to 22.2 ms. Frames over 50 ms went from 252 to 48 (median per run).

**Caveats, please read:**

- **The measured build is not plain v0.4.7.** It is v0.4.7 plus my fork's render-thread work (draws deferred to the render thread and related). The two executables differ only in `SFR_FAST_PATH();`, which a post-processor added to the same generated sources. I have not timed this exact branch on plain v0.4.7 yet.
- **The executable grows from 114 MB to 162 MB.** Each access carries its inlined fast path. This may matter more on Android.
- **Coverage is limited.** Only Windows, x86, D3D12 and `SFR_PARALLEL_WORKER=cores` were tested. Nothing has run on Android/AArch64, Vulkan or the handhelds.
- **What I could run on Linux here:** `sfr_memory_test` and the Python tests (299) pass. A function shaped like the generated code compiles with the new macros. The generated game code itself was only built on Windows, in the fork.
