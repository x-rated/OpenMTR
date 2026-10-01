# OpenMTR Architecture

Read this first. It tells you which file owns what, so you can jump straight
to the right place instead of grepping the whole tree. Deeper dives live in
the other docs/ files, referenced inline below.

Derived from WinMTR Redux / WinMTR (GPL v2) — see `tracer.h`'s file header.

## The shape of the app

OpenMTR is one binary that is either a Qt Widgets GUI or a headless CLI,
decided by argv before any Qt object is constructed:

```
main.cpp
 ├─ "--version"/"-v" anywhere in argv → print version, exit (no Winsock, no Qt)
 ├─ "--report" anywhere in argv       → cli::run(argc, argv), exit (no QApplication)
 └─ otherwise                          → normal GUI: QApplication + MainWindow + exec()
```

Everything that isn't UI chrome or CLI plumbing is **shared** between the two
front ends, on purpose, so they can never disagree about what a trace found
or how a report is formatted:

| Shared module | Owns | Used by |
|---|---|---|
| `tracer.{h,cpp}` | The actual ICMP engine — `OpenMTRNet` (dispatch loop, per-hop state) and `OpenMTRNetWrapper` (threaded facade) | MainWindow (via wrapper) and cli.cpp (via wrapper) identically |
| `report_core.{h,cpp}` | Turning an engine snapshot into display-ready rows; text/JSON/CSV formatting; warm-up/settling state machine (`warmupRouteSettled`, `dnsAndAsnSettled`) | MainWindow's table + Copy/Export; cli.cpp's `--report` output |
| `asncache.{h,cpp}` | ASN lookup (Team Cymru DNS-TXT) with a background-thread cache, no Qt event loop required | MainWindow's table; cli.cpp |
| `updatecheck.{h,cpp}` | GitHub release check via a `curl` subprocess, gated by one compile-time switch | MainWindow's About/badge; cli.cpp's startup notice |
| `versioninfo.{h,cpp}` | The one-line version string (`"1.4.0 DEV (AMD64) · Qt 6.8.1"`) | MainWindow's About dialog; `--version` |

Front-end-only code:

| File | Owns |
|---|---|
| `MainWindow.{h,cpp}` | Everything visual: frameless window chrome, theming, the results table, dialogs. See `docs/GUI_INTERNALS.md`. |
| `cli.{h,cpp}` | Argument parsing, the blocking warm-up/counting loop, progress line, `--output`, console-vs-redirected output handling on Windows (`writeText`). See its own file-header comment in `cli.cpp` for the exit-code table. |
| `main.cpp` | Entry point dispatch (above), Winsock init/teardown, per-platform `QApplication` setup (DPI rounding, AppImage desktop-entry self-registration, GTK theme override). |

## Data flow for one trace

1. **Start.** GUI: `MainWindow::onStartStop()` resolves the target, creates an
   `OpenMTRNetWrapper`, calls its threaded `DoTrace()`. CLI: `cli::run()` does
   the same resolve-and-start sequence directly (see `resolveTarget()` in
   `cli.cpp` — deliberately mirrors the GUI's Start path so a target behaves
   identically either way).
2. **Warm-up.** Both drive the *same* state machine
   (`report_core.h`'s `warmupRouteSettled()` / `dnsAndAsnSettled()`): wait
   until the discovered route's fingerprint (hop count + every address) holds
   steady, every checked hop has answered or exhausted a timeout guard, and
   DNS/ASN lookups have settled or given up — or an overall deadline passes.
   GUI drives this via `QTimer::singleShot` chaining
   (`MainWindow::onWarmupEnd()`); CLI drives it via a blocking sleep loop.
   Same conditions, different pump.
3. **Reset & count.** `OpenMTRNet::ResetStats()` zeroes counters so displayed
   statistics describe only the counting window, not warm-up traffic.
4. **Live updates.** GUI: `MainWindow::updateTable()` polls the wrapper on a
   timer and calls `computeReportRows()` (`report_core.h`) to turn the
   engine snapshot into table rows. CLI: nothing until the run ends (no
   incremental output — just the stderr progress line).
5. **Stop & report.** Both call `computeReportRows()` one final time and hand
   the result to `buildTextReport()`/`buildJsonReport()`/`buildCsvReport()`
   (`report_core.cpp`). GUI shows it via Copy/Export; CLI prints it to
   stdout or writes it via `--output`.

Because steps 2–5 run through the same functions, a GUI export and a CLI
`--report --json` for the identical target/timing produce byte-identical
report content.

## Build targets & platform split

One `CMakeLists.txt`, three shapes (see `docs/BUILD_AND_CI.md` for the full
CI pipeline):

- **Windows**: static Qt, linked as a **console-subsystem** exe (not
  `WIN32_EXECUTABLE`) so shells wait for the CLI modes, with
  `consoleAllocationPolicy=detached` in `resources/app.manifest` so the GUI
  opens without a console window on Windows 11 24H2+ (see
  `docs/KNOWN_ISSUES_AND_HISTORY.md`), AMD64 + ARM64 (ARM64 cross-compiled via MSVC).
- **macOS**: static Qt, Apple Silicon only (no Intel), `.app` bundle → `.dmg`.
- **Linux**: static Qt, AMD64 + ARM64, packaged as an AppImage.

All three link Qt **statically** and trim unused Qt features — see
`ci/qt-{linux,macos,windows}.conf`. None of them link `Qt6Network`; the
update check shells out to `curl` instead (`updatecheck.cpp`). A large part
of `CMakeLists.txt` and the CI workflow exists to keep the shipped binaries
from tripping antivirus ML heuristics — that whole strategy is documented
separately in `docs/AV_HARDENING.md` because it's easy to accidentally
undo one piece of it (e.g. "helpfully" re-enabling AppImage/DMG compression)
without realizing why it was disabled.

## Repo layout

```
CMakeLists.txt, LICENSE, README.md, .gitattributes   — root, GitHub/CMake convention
.github/workflows/build.yml                          — CI (build+package+smoke-test, 3 platforms)
src/                                                  — all application source (see table above)
resources/                                            — icons, .qrc, .manifest, .rc.in, .plist.in, .desktop
ci/                                                    — Qt build configs (qt-linux/macos/windows.conf)
packaging/                                             — macOS DMG background art, icon SVG source
tests/                                                 — engine_tests.cpp — tracer.cpp in isolation, no Qt/window
docs/                                                   — this documentation set
```

Moved from a flat root layout into `src/`/`resources/` on 2026-09-27 — if
you're reading an older commit or a stray reference to a root-level
`main.cpp`/`app_icon.png`, that's why.

## Where to go next

- Touching the ICMP engine, warm-up timing, or per-platform packet handling → `docs/ENGINE_INTERNALS.md`
- Touching the window chrome, theming, or any dialog → `docs/GUI_INTERNALS.md`
- Touching CMakeLists.txt, the CI workflow, or anything about how binaries are packaged/signed → `docs/BUILD_AND_CI.md`
- Wondering "didn't we already fix this" or hitting a weird platform quirk → `docs/KNOWN_ISSUES_AND_HISTORY.md`
- About to make a change and want the "don't forget to also update X" list → `docs/CONTRIBUTING_CHECKLIST.md`
