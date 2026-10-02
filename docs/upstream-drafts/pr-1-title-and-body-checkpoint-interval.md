Title: perf: let the guest checkpoint call into the permit every 256 entries, not 32

---

Refs #33.

## What

The generated code calls `guest_checkpoint()` at every function entry and loop label. One call in 32 reaches `guest_checkpoint_permit()` (a thread-local read, an ownership check, and a clock read on every 64th call). This changes 32 to 256.

`SFR_CHECKPOINT_INTERVAL=N` sets it, so the old value can be compared and a device can pick its own. The default is 256. Two files, 18 lines added (the change and a comment).

## Why it should be safe

`Lease::checkpoint` reads the clock only on every 64th call into it, so a handoff or a cancellation is noticed after about 64 x 256 entries instead of 64 x 32. At ten million entries a second (an estimate, not measured) that is about 1.6 ms instead of 0.2 ms, against the 2 ms scheduling quantum. The delay is bounded; nothing is skipped. Stop and cancellation take the same path.

## Measurements

Build with the switch, same executable, interval 32 against 256, six rounds each, settings alternating within a round, 3468 race frames per run, D3D12. Ratio = fps at 32 / fps at 256, same round.

| PC | Per-round ratios | Median | Rounds where 32 was faster |
| --- | --- | ---: | ---: |
| i5-3470 / RX 480 | 0.97 0.99 0.93 0.92 0.94 0.94 | 0.94 | 0 of 6 |
| i7-6850K / RTX 3080 Ti | 0.91 0.99 0.93 0.94 0.91 0.97 | 0.94 | 0 of 6 |
| Ryzen AI MAX+ 395 | 0.93 0.95 0.91 0.96 0.89 0.92 | 0.93 | 0 of 6 |

**Caveats, please read:**

- These were measured on **v0.4.5**. v0.4.6's targeted permit wakeups make a handoff cheaper, so the gain may be smaller there. I have not re-measured on v0.4.6 yet.
- The runs used a fixed order with 256 first. The same executable run twice showed the second run of a round about 2.4% slower in 6 of 6 rounds, so up to that much of the numbers above can be order, not interval. `pr/benchmark-harness` now turns the order each round.
- Windows, x86, D3D12 only. Nothing on Android or AArch64, where handoff costs differ and the best value may too.
- `diagnostic_main.cpp` is not compiled by the Linux build, so I could not compile this exact file here; the same logic ran in a fork build on the three PCs above.

## How to check

```powershell
$env:SFR_CHECKPOINT_INTERVAL = '32'   # the previous behaviour
```

or `scripts/benchmark.ps1 -Configs baseline,checkpoint-32` from `pr/benchmark-harness`.

Generated with [Claude Code](https://claude.com/claude-code)
