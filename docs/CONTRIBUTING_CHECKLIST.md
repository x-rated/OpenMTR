# Contributing Checklist

Quick "if you're about to do X, also check Y" reference. See
`docs/ARCHITECTURE.md` for the module map these refer to.

## Adding/changing anything in the traced report (a column, a status text, a format)
- [ ] Edit `report_core.{h,cpp}` only — never MainWindow or cli.cpp directly;
      both consume the shared functions.
- [ ] If you touch `Column`/`COLUMNS`: MainWindow adds two more
      *rendering-only* spacer columns on top (`ColCount`, `ColCount+1`) —
      don't confuse these with real data columns.
- [ ] Test from **both** front ends: GUI table + Copy/Export, and
      `./OpenMTR --report --time 5 --json/--csv/--txt 127.0.0.1`.
- [ ] CSV: hostname-column formula-injection guarding
      (`report_core.cpp` ~L139) must stay hostname-only — other columns are
      app-generated and prefixing them would distort data.
- [ ] Update `README.md`'s CLI section and `printUsage()` in `cli.cpp` if
      the change is user-visible from the command line.

## Adding a new CLI argument
- [ ] `CliOptions` struct, `parseArgs()`, `printUsage()` (all `cli.cpp`).
- [ ] `README.md`'s CLI options table and examples.
- [ ] New exit code? Add to the `ExitCode` enum, `printUsage()`'s exit-code
      list, and `README.md`'s exit-code table — all three, or they drift.
- [ ] Test: `--help` shows it; a real run exercises it; if it's a format
      flag, check mutual-exclusivity behavior with `--txt`/`--json`/`--csv`.

## Touching the ICMP engine (`tracer.cpp`/`tracer.h`)
- [ ] Read `docs/ENGINE_INTERNALS.md`'s per-platform quirks table first —
      Windows/Linux/macOS diverge in non-obvious, empirically-discovered
      ways (error-queue delivery, echo-id filtering, receive buffer size).
      A "simplify this, it looks duplicated" instinct is usually wrong here.
- [ ] Run `tests/engine_tests.cpp` (separate `-DOPENMTR_BUILD_TESTS=ON`
      build) — it exists specifically to catch regressions of three
      previously-shipped bugs (see `docs/KNOWN_ISSUES_AND_HISTORY.md`).
- [ ] Anything touching `m_mutex`/atomics: check whether a caller could now
      observe a torn read across two locked calls instead of one — prefer
      adding a field to an existing single-lock snapshot over a new
      standalone accessor.
- [ ] New OpenMTR-specific status code? Base it off `OPENMTR_STATUS_BASE`
      (12000), never adjacent to the Windows `IP_STATUS` range (Microsoft
      owns 11000-11050 and may extend it).

## Touching the update-check feature
- [ ] The *only* switch is `OPENMTR_ENABLE_UPDATE_CHECK` in `CMakeLists.txt`
      — never add a second, independent way to disable it in just one of
      MainWindow/cli.cpp, or the two front ends can end up disagreeing.
- [ ] Read `docs/AV_HARDENING.md` before adding any networking call here —
      this feature exists specifically without Qt6Network for AV reasons.
- [ ] Keep it a plain **blocking** function
      (`checkForUpdateBlocking()`) — GUI callers wrap it in their own
      background thread + `QMetaObject::invokeMethod` marshal-back; don't
      make the shared function itself threaded/async, or the CLI (no event
      loop) breaks.

## Touching Windows console/subsystem handling (`WIN32_EXECUTABLE`, `app.manifest`, `cli.cpp`'s `writeText`)
- [ ] Read the "Windows GUI exe as a CLI" entry in
      `docs/KNOWN_ISSUES_AND_HISTORY.md` in full — the whole CLI-on-Windows
      story rests on two things that must stay together: the exe is a
      **console-subsystem** image (no `WIN32_EXECUTABLE`), and the manifest
      says `consoleAllocationPolicy` = `detached`. Removing either one breaks
      it (shells stop waiting, or the GUI opens a console window). CI checks
      both on the built exe.
- [ ] Don't reintroduce console plumbing (`AttachConsole`, `freopen`,
      `_dup2` of stdio, cursor/prompt manipulation, injected keystrokes) —
      if a shell misbehaves with the current build, find out why rather than
      compensating for it in the app.
- [ ] Test on a real **Windows 11 24H2+** machine (Wine can't tell you any of
      this — it doesn't implement console allocation policy, `WriteConsoleW`
      rendering, or shells that wait): from Explorer / Start menu the GUI
      opens with **no console window**; from PowerShell and cmd
      `OpenMTR --report --time 5 <target>` blocks until done, prints a fresh
      prompt on its own line afterwards, and sets `$LASTEXITCODE` /
      `%ERRORLEVEL%`; `--version` prints "·" correctly (not "┬Ě" or "?") both
      directly in the console and via `--version | Out-File` /
      `--version > out.txt` (the file should contain plain UTF-8 bytes, not
      UTF-16); a report with a non-ASCII hostname prints correctly the same
      two ways.
- [ ] Also check the GUI from Explorer on an older Windows 11 build (23H2 or
      earlier), where the manifest element is ignored: a console window
      behind the GUI is the known, accepted outcome there.

## Touching MainWindow's frameless window / theming
- [ ] Read `docs/GUI_INTERNALS.md` first — the three platforms use three
      genuinely different strategies (native-frame-hidden on Windows,
      fully-hand-painted on Linux, native-title-bar on macOS), not one
      abstraction with platform `#ifdef`s sprinkled in.
- [ ] Changing `kShadowMargin`/`kWindowCornerRadius`/`kResizeMargin`? Check
      `ovResizeBandInset()` and `updateLinuxInputShape()` together — the
      clickable pixels and the "this counts as an edge grab" pixels are
      deliberately kept from drifting apart by sharing one function.
- [ ] Any new copy-to-clipboard code path: use the hand-rolled Win32
      `copyTextToClipboard()`, not Qt's own clipboard API — the static
      build's own clipboard write is broken (see
      `docs/KNOWN_ISSUES_AND_HISTORY.md`).
- [ ] New dialog? Check whether it needs the Linux stylesheet
      parent-chain-cascade fix (an inherited `transparent` background
      collapsing to solid black) the Export dialog needed.

## Touching CMakeLists.txt or the CI workflow
- [ ] Read `docs/AV_HARDENING.md` in full before removing anything that
      looks like unnecessary complexity around compiler flags, debug info,
      or packaging compression — most of it is a direct response to a
      confirmed false-positive detection, not incidental.
- [ ] Moving/renaming a source or resource file? There are (at time of
      writing) four places that reference file paths outside
      `qt_add_executable`'s own list: `resources/app.rc.in`'s icon path,
      `tests/CMakeLists.txt`'s `tracer.cpp` path + include dir, and the
      AppImage packaging step's `cp openmtr.desktop`/`cp app_icon.png`
      lines in `build.yml` (×2 each). Grep the old filename across
      `CMakeLists.txt`, `tests/CMakeLists.txt`, `resources/app.rc.in`, and
      `.github/workflows/build.yml` before considering a move finished.
- [ ] Changing the ARM64 Windows runner or `MSVC_TOOLSET`? Re-read the
      "windows-11-vs2026-arm" note in `docs/BUILD_AND_CI.md` — this is a
      moving target tied to an in-progress GitHub Actions migration.

## Renaming/moving any file in the repo
- [ ] Update `.gitattributes` if the pattern was file-specific (the current
      `*.h linguist-language=C++` is extension-based and survives moves
      unchanged, but don't assume every future entry will be).
- [ ] See the CMakeLists.txt checklist item above — the same four
      cross-references apply to any resource file, not just during the
      2026-09-27 `src/`/`resources/` restructuring.
