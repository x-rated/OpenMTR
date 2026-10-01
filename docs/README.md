# OpenMTR Developer Docs

Written for both humans and AI assistants picking up this codebase, so
future analysis doesn't have to re-derive context that's already been
worked out once. Start with `ARCHITECTURE.md`; the rest are deep dives you
only need when you're actually touching that area.

| File | Read this when... |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Starting out. Module map, data flow, shared vs. platform-specific code, repo layout. |
| [ENGINE_INTERNALS.md](ENGINE_INTERNALS.md) | Touching `tracer.{h,cpp}` — the ICMP dispatch loop, per-platform packet handling, thread safety, warm-up logic. |
| [GUI_INTERNALS.md](GUI_INTERNALS.md) | Touching `MainWindow.{h,cpp}` — the frameless window chrome, theming, dialogs. |
| [AV_HARDENING.md](AV_HARDENING.md) | Touching build flags, `updatecheck.cpp`, or CI packaging — before removing anything that looks like unnecessary complexity. |
| [BUILD_AND_CI.md](BUILD_AND_CI.md) | Touching `CMakeLists.txt` or `.github/workflows/build.yml`. |
| [KNOWN_ISSUES_AND_HISTORY.md](KNOWN_ISSUES_AND_HISTORY.md) | Hitting a weird platform quirk, or wondering "didn't we already fix this?" |
| [CONTRIBUTING_CHECKLIST.md](CONTRIBUTING_CHECKLIST.md) | About to make a change — the "also update X" list by change category. |

These docs summarize and index what's already explained in code comments —
the source is extremely well-commented (roughly a quarter of `MainWindow.cpp`
is comments) and remains the authority for exact behavior. Treat these as a
map to the right place, not a replacement for reading the function you're
about to change.

Last written: 2026-09-27, covering the codebase as of the `src/`/`resources/`
restructuring. If it drifts out of sync with the code, trust the code and
fix the doc.
