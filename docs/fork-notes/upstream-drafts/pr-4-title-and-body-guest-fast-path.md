Opened as https://github.com/YuutaTsubasa/Free-Riders-Recompiled/pull/38 (2026-10-04).

Title: perf: take guest memory's fast path once per generated function

---

Refs #33. Branch: [`pr/guest-fast-path`](https://github.com/knuckleslee/Free-Riders-Recompiled/tree/pr/guest-fast-path), one commit (plus two doc commits) on `main` (`e6fc52e`, v0.4.7). 6 files, +185 / -10.

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

- **Machine:** i5-3470 with an RX 480, D3D12, Windows 10, `SFR_PARALLEL_WORKER=cores`.
- **Builds:** both from this branch on v0.4.7. The branch's generator output was built as is (fast path), and again with every `SFR_FAST_PATH();` line removed (plain). The stripped sources were checked to be byte-identical to what the unchanged generator produces.
- **Runs:** benchmark harness, six interleaved rounds plus one uncounted warm-up. All 13 runs ended normally at their present limit.
- **Frame rate:** taken over the same stretch of race time (seconds 20 to 75), so a faster build does not get a different part of the race.

| | plain | fast path | change | per-round ratio | faster rounds |
| --- | ---: | ---: | ---: | --- | ---: |
| fps, race seconds 20–75 | 25.6 | 29.4 | **+15%** | 1.13 1.13 1.18 1.16 1.16 1.19 | 6/6 |

- The plain build's own spread was 5% (24.8–26.1 fps). The median gain was +16%.
- Main-thread permit hold per frame went from 23.0 to 19.7 ms, P95 frame time from 49.3 to 42.1 ms, and frames over 50 ms from 149 to 22 (median per run).

An earlier run on my fork (v0.4.7 plus its render-thread work, same marker-only difference) gave 24.9 → 27.5 fps, +11%, 6/6 rounds.

**Caveats, please read:**

- **The executable grows from 114 MB to 162 MB.** Each access carries its inlined fast path. This may matter more on Android.
- **Coverage is limited.** Only Windows, x86, D3D12 and `SFR_PARALLEL_WORKER=cores` were tested. Nothing has run on Android/AArch64, Vulkan or the handhelds.
- **What I could run on Linux here:** `sfr_memory_test` and the Python tests (299) pass. A function shaped like the generated code compiles with the new macros.
