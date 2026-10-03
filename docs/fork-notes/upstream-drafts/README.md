# Drafts for the original repository

State on 2026-10-03: the maintainer merged the benchmark harness (#34), the checkpoint
interval (#35) and the render thread (#36) from issue #33, keeping authorship and adding
fixes (Windows-only 256 default with strict parsing, failure handling at GPU teardown,
Vulkan coverage, benchmark isolation). So:

- `pr-1`, `pr-2`, `pr-3` and `issue-1` to `issue-3` are **superseded**: nothing is left to open.
- `issue-4-comment-v0.4.6-render-thread.md` is still a useful follow-up for #33.
- `performance-data-v0.4.5.md` is the full data behind the issue.

Sent on 2026-10-04:

- `issue-5-comment-all-mode.md`: `all` against `cores` for #33, from fork builds only. **Posted** on 2026-10-04:
  https://github.com/YuutaTsubasa/Free-Riders-Recompiled/issues/33#issuecomment-5971867013
- `pr-4-title-and-body-guest-fast-path.md`: the guest-memory fast path, branch `pr/guest-fast-path` on
  v0.4.7 (`e6fc52e`). Timed on plain v0.4.7 (+15%, 6/6 rounds). **Opened** on 2026-10-04 as
  https://github.com/YuutaTsubasa/Free-Riders-Recompiled/pull/38
