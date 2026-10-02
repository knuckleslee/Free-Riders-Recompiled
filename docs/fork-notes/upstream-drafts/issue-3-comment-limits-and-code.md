Limits, earlier observations and the code branches for the issue above.

## Limits to keep in mind

- One scenario (a Free Race, one course and character, scripted menus) on three x86 PCs with D3D12. Nothing here speaks for Android, AArch64, Vulkan or the handhelds this project targets.
- The run is uncapped. On Z13 a frame is about 10 ms, so a fixed per-frame cost shows as a larger percentage there than it would at a 60 fps cap.
- Six rounds per setting; the baseline's own spread is 4-8%. Effects of 3% or less are suggestions, not findings.
- The build is v0.4.5 plus the fork's changes, as listed. v0.4.6 changed scheduling, entry overhead and texture uploads, so these numbers should be re-checked there before acting on them.

## Earlier observations on older bases (not re-measured on v0.4.5)

Measured on i7 around v0.4.0-v0.4.3, listed because they are cheap negative results:

- Waking only the next waiting guest did not change the race frame time (hand-off gap stayed 2.6-5.0 ms). The gap was the host scheduling the next guest, not waking too many.
- Letting the main thread spin for its turn at the permit was neutral (-1%).
- A main-thread profile of a race: about 34% of samples in the generated game code, 30-34% outside the executable (waits, driver, system), 14-17% drawing, 6% checked guest-memory accesses; `__savegprlr`/`__restgprlr` about 2.6% before the localized build and 1.3% after.
- A frame's main-thread permit queueing (4-8 ms) split about half between "behind another guest" and "permit free but the main thread not yet running".

## Code

Branches on top of current `main` (`17506ad`, v0.4.6) are on my fork, so you can read them before deciding. I have not opened PRs; say which, if any, you want as PRs.

| Branch | What | State |
| --- | --- | --- |
| [`pr/checkpoint-interval`](https://github.com/knuckleslee/Free-Riders-Recompiled/tree/pr/checkpoint-interval) | checkpoint call every 256 entries; `SFR_CHECKPOINT_INTERVAL` to change it | one file; data above is on v0.4.5 |
| [`pr/render-thread`](https://github.com/knuckleslee/Free-Riders-Recompiled/tree/pr/render-thread) | render thread, on by default for D3D12 only; `SFR_RENDER_THREAD=0/1` | ordering test passes on a software Vulkan driver; never run on a device |
| [`pr/benchmark-harness`](https://github.com/knuckleslee/Free-Riders-Recompiled/tree/pr/benchmark-harness) | the benchmark scripts, their Python tests, three small program additions (`racing=` field, menu pacing, end after the last word) | scripts ran on Windows against v0.4.5 in my fork; this trimmed version has not run on Windows yet |

On all three, the Linux build passes and 122 of 123 tests pass; the one failure
(`native_presentation`, a swap-chain growth check) fails the same way on
unmodified `main` under a software Vulkan driver. None of them has been run on a
Windows PC against v0.4.6 yet.

I did not branch the register-locals post-processor (about 4%, high risk), the
store-check skip (about 3%) or the two changes with no effect.
