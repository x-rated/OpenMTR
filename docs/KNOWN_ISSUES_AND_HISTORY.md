# Known Issues & Fix History

Two kinds of entries: bugs that shipped once and were fixed (don't
reintroduce them — several have a regression test), and external
platform/tooling quirks that aren't OpenMTR bugs at all but needed a
workaround anyway.

## Bugs that shipped and were fixed

### Reverse-DNS use-after-free (1.3.0)
An engine destroyed while a reverse-DNS worker it started was still running
would write into freed memory. The original fix was lost in an upload
(commit `0f67645`). **Regression test**: `tests/engine_tests.cpp`'s
`dns_lifetime` — against a regressed tree this aborts ("mutex lock failed")
or hangs (that's what the `ctest` TIMEOUT is for). Current fix: `DnsSink`,
a `shared_ptr` jointly owned by the engine and every worker
(`tracer.h` ~L266). See `docs/ENGINE_INTERNALS.md`.

### IPv6 route length stuck at MAX_HOPS when hop 1 never answers
`RecalcMaxLocked()` used to infer the trace's address family from hop 1's
address — an eternally-silent hop 1 meant the family was never determined
and `GetMax()` stayed pinned at `MAX_HOPS` forever, even once later hops
(including the destination) had answered. Regression test:
`v6_silent_first_hop`.

### Stop landing before/right after Start
`StopTrace()` before `DoTrace()` even began used to get overwritten by
`DoTrace()` unconditionally setting `tracing = true`, leaving a trace
running with nothing left to stop it. Fixed by the two-step
`stopRequested`/`tracing` ordering described in `docs/ENGINE_INTERNALS.md`.
Regression tests: `stop_before_start`, `stop_right_after_start`.

### Windows GUI exe as a CLI: output, prompt, exit code (2026-09)
One root cause, five symptoms: OpenMTR was linked as a GUI-subsystem
executable (`WIN32_EXECUTABLE`), and **cmd.exe and PowerShell do not wait for
a GUI-subsystem child.** They print the next prompt immediately and move on:
- output landing after an already-drawn prompt, the shell never redrawing it
  (just a blinking cursor until Enter is pressed);
- `$LASTEXITCODE` staying empty;
- PowerShell's `>` leaving the file empty (see the entry under "External
  quirks" below);
- a GUI-subsystem CRT wiring `printf`/`fputs` to *nothing* — not a console,
  and not even a redirected file/pipe whose handle `GetStdHandle()` reports
  as valid;
- no console at all to attach to without `AttachConsole`.

The first solution treated these one by one, all inside a `ConsoleSession`
class in `cli.cpp`: `_dup2` the CRT's fd 1/2 onto an existing handle, else
`AttachConsole(ATTACH_PARENT_PROCESS)` + `freopen("CONOUT$")`; wait for the
shell to finish drawing its prompt, blank that stale line via
`FillConsoleOutputCharacter`, print from column 0, and at exit inject an
Enter keystroke with `WriteConsoleInput` so the shell drew a fresh prompt.
(`FreeConsole()` was also tried and did nothing for the prompt — the shell
had long since returned.) It worked, but it was a pile of timing-dependent
console manipulation that could only be tested on real Windows.

**Current solution: fix the cause instead of the symptoms.** Windows 11 24H2
added the `consoleAllocationPolicy` manifest element
([Microsoft Learn: Console Allocation Policy](https://learn.microsoft.com/en-us/windows/console/console-allocation-policy)).
OpenMTR is now built as a **console-subsystem** executable
(`/SUBSYSTEM:CONSOLE`, `WIN32_EXECUTABLE` deliberately off in
`CMakeLists.txt`), and `resources/app.manifest` sets
`consoleAllocationPolicy` to `detached`. The console subsystem is what tells
shells to wait for the process and gives it normal stdio; `detached` is what
stops Windows from creating a console window when the exe is started from
Explorer, so the GUI still looks like a plain GUI app. None of the workarounds
above remain. The one console-related concern left is unrelated to the
subsystem question (next bullet).

- **Console output encoding** — a console's output code page is not UTF-8 by
  default (852 on a Czech system): "·" (bytes C2 B7) showed as "┬Ě". The first
  fix here, `SetConsoleOutputCP(CP_UTF8)` around the run, looked right per
  Microsoft's own docs but did not reliably work in practice: a console's
  code page and its font are separate settings, older conhost's raster
  ("Terminal") font cannot render code page 65001 at all regardless of what
  `SetConsoleOutputCP` reports, and buffered CRT stdio (`printf`/`fputs`)
  writing to a console does not consistently honor the changed code page
  either — "┬Ě" kept appearing. `writeText()` in `cli.cpp` now sidesteps code
  pages entirely: for a real console it converts to UTF-16 and calls
  `WriteConsoleW`, which Windows always renders correctly no matter the
  console's code page or font; for output redirected to a file or a pipe
  (`GetConsoleMode` fails) it writes UTF-8 bytes, as before and as the
  non-Windows build always has.

Consequences of this design, worth knowing before touching it:
- **Windows before 11 24H2 (build 26100) ignores `consoleAllocationPolicy`.**
  The CLI is unaffected (the subsystem alone is what makes shells wait), but
  starting the GUI from Explorer, the Start menu or a shortcut there also
  opens an empty console window behind it. This is the price of not shipping
  two binaries.
- **Started from a shell with no arguments** (`.\OpenMTR.exe`), the GUI now
  blocks that shell until the window is closed, like any console-subsystem
  program. `start OpenMTR.exe` (cmd) / `Start-Process OpenMTR.exe`
  (PowerShell) launch it without waiting.
- The CI job verifies both halves on the built exe (subsystem = Windows CUI,
  manifest contains `detached`), because losing either one produces no
  build error.
- **Not yet verified on real Windows**: the behaviour above follows from
  Microsoft's documentation and the nature of the old PowerShell bug, not
  from a recorded run on Windows 11 24H2 with PowerShell. Run the scenarios in
  `docs/CONTRIBUTING_CHECKLIST.md` and update this entry with the outcome.

The alternative that was rejected — two binaries (a console launcher next to
the GUI exe, like Visual Studio's devenv.com/.exe) — would work on every
Windows version, at the cost of a second artifact and a tiny extra
executable, the kind of file AV heuristics tend to look at suspiciously.

### "Finishing..." progress line running into the report on stdout
`Progress`'s line-clear only happened in its destructor, which fires at
function-scope end — *after* the report had already been written to stdout.
On a real terminal this visually ran the report's first line directly onto
the end of "Finishing..." with no line break. Fixed by an explicit,
idempotent `progress.finish()` call right before the report is printed;
the destructor is now just a safety net for early-return paths.

## External quirks (not OpenMTR bugs, but needed a workaround)

### PowerShell silently dropped `>` redirection for the GUI-subsystem exe (resolved)
**Confirmed, unresolved upstream PowerShell issue**:
[PowerShell/PowerShell#25875](https://github.com/PowerShell/PowerShell/discussions/25875).
For a *GUI-subsystem* executable, PowerShell's `>` hands the process a
**pipe**, not a real file handle, and does not wait for the process to
finish writing before returning control to the prompt — the destination
file was silently left empty and `$LASTEXITCODE` stayed unset (PowerShell
considered "launching the process" the whole job). `cmd.exe` and every Unix
shell were unaffected.

This was specific to GUI-subsystem executables, and OpenMTR no longer is one
(see the entry above), so PowerShell now treats it like any other native
command. `--output <path>` (`cli.cpp`), which was introduced as the
workaround, stays as an ordinary feature: it also works with no shell or
console at all (Task Scheduler, a service). It is no longer needed just to
get a file out of PowerShell.

### GitHub Linguist misclassifying a `.h` file as C
Linguist's C/C++/Objective-C disambiguation for `.h` is heuristic (Bayesian
classifier over content tokens like `class`/`namespace`/`template`/`::`) —
`updatecheck.h`, containing only a plain `struct` with Qt types and no
strong C++-only token, got classified as C. **Fix**: `.gitattributes` at the
repo root with `*.h linguist-language=C++`, forcing every header in the
repo to count as C++ rather than being guessed per-file.

### GitHub Actions `windows-11-arm` runner label mid-migration
See `docs/BUILD_AND_CI.md`'s Windows section — `actions/runner-images#14602`,
worked around by pinning to the explicit `windows-11-vs2026-arm` label.

### Wacatac ML false positives (Windows `.exe`, macOS `.dmg`, Linux `.AppImage`)
See `docs/AV_HARDENING.md` in full — this is a large, deliberate,
cross-cutting mitigation strategy, not a one-line fix.

### Static Qt build's clipboard is broken
This project's statically-linked Qt build's own clipboard write doesn't
work — `QLineEdit`'s built-in Ctrl+C silently does nothing. Every copy
operation in the app goes through a hand-rolled Win32
`copyTextToClipboard()` instead (`MainWindow.cpp` ~L2206). If a future
Qt/build upgrade fixes this upstream, this workaround could in principle be
removed, but verify with an actual clipboard paste test on all three
platforms first — don't assume it's fixed just because a changelog mentions
clipboard work.

### macOS `net.inet.raw.recvspace` default (8KB)
Not really a "bug," but easy to rediscover the hard way: macOS/BSD's
default ICMP raw-socket receive buffer is 8KB, and an echo reply for a probe
size at or above roughly 8133 bytes silently doesn't fit — every probe at
large `--size` values times out with no error. Worked around by explicitly
setting `SO_RCVBUF` to 64KB in `tracer.cpp`; a refusal to set it is not
treated as fatal (falls back to whatever the OS default allows).

### `ilammy/msvc-dev-cmd` GitHub Action is unmaintained
Last touched Jan 2024, still on the deprecated Node 20 runtime. Replaced
with `TheMrMilchmann/setup-msvc-dev` (actively maintained, Node 24, same
`arch`/`toolset` input shape) — if you see the old action name anywhere,
it's stale and should be swapped.
