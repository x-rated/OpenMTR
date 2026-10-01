# Build System & CI

See `docs/AV_HARDENING.md` first if you're touching anything related to
compiler/linker flags or packaging compression — a lot of what looks unusual
here is deliberate AV false-positive mitigation, not incidental complexity.

## Static Qt everywhere

Every platform links a **static, feature-trimmed Qt built from source** in
CI (flags in `ci/qt-{linux,macos,windows}.conf`), not a system/distro Qt and
not Qt's official dynamic binaries. Reasons, per platform:

- **Windows**: a single portable `.exe` with no Qt DLLs to ship alongside it.
- **macOS**: earlier iterations used Homebrew's Qt (silently required the
  runner's own newer macOS despite declaring 12.0, plus ~35MB of
  ICU/glib/dbus) and then official dynamic binaries (correct, but ~22MB of
  bundled frameworks for what's otherwise a ~10MB DMG). Static-trimmed
  matches the Windows approach and keeps both similarly small. No
  `macdeployqt`, no bundled frameworks, no plugin pruning — the whole
  bundle is one binary; `CMakeLists.txt` links the Cocoa platform plugin in
  statically.
- **Linux**: `-static` isn't available from apt's Qt package at all, and
  `-no-icu` avoids `libicui18n`/`libicuuc`/`libicudata` (both the distro
  package and Qt's own official binaries link these; the only real loss
  without them is locale-aware collation via `QCollator`, unused here). The
  result needs nothing from the Qt build at runtime — only ordinary system
  libraries every desktop already has (glibc, libstdc++, X11/xcb, D-Bus).

Because a static build means `qt_import_plugins()` can't auto-discover the
platform plugin the way it would for a dynamic build, `CMakeLists.txt`
imports each platform's QPA plugin explicitly (`~L124`). Qt caches key off a
hash of the relevant `.conf` file, so editing Linux's Qt config only
invalidates Linux's cache, and there's deliberately no manual cache-busting
counter (forgetting to bump one meant silently shipping artifacts built by
the *previous* configuration — the hash approach can't have that failure
mode).

**Linux windowing scope**: xcb only (plain X11 + XWayland, i.e. the large
majority of "Wayland" desktops too, transparently). A native-Wayland-only
session with no XWayland would need `qtwayland` built and statically
linked too — its own build-time deps (wayland-scanner, wayland-protocols,
libxkbcommon) are left out for now as a size/complexity trade-off, not
because it's impossible.

## macOS-specific build notes

- **Apple Silicon (arm64) only** — no Intel build. This is why
  `MainWindow.h`'s Cocoa accent-color code (`~L183`) can use the plain
  `objc_msgSend` entry point without the dedicated `objc_msgSend_fpret`
  x86_64 needs for `CGFloat`-returning selectors — there's no x86_64 target
  to need it.
- **Deployment target (14.4) must be set before `project()`**, and must
  match the minimum the Qt in use supports (Qt 6.12: 14.4; Qt 6.11 and
  earlier: 13) — declaring more excludes working systems; declaring less lets macOS launch
  a bundle whose Qt then refuses to load, crashing at startup instead of
  showing a clean version-requirement dialog. CI independently re-checks
  this against the *built* binary's actual minimum (via `sort -V` full
  version comparison, not just major-version, since a library needing 14.5
  in a bundle declaring 14.4 still crashes on 14.4) — this is the exact
  regression the check exists to catch: a bundle promising one macOS version
  while its libraries need a newer one, launching fine on the CI runner but
  dying with `dyld` errors on users' machines.
- CI also does a **real smoke test**: launches the built `.app` on the
  runner's actual Aqua session and requires it to still be alive after 8s —
  catches `dyld` kills, missing plugins, unrecognized-selector aborts at
  startup. Only proves it works on the runner's own macOS release, but
  strictly better than never executing the built binary at all (which is
  how the deployment-target regression above first shipped unnoticed).
- **App icon**: macOS uses its own master (`app_icon_macos.png`), not the
  `app_icon.png` other platforms share. Since macOS 26, Finder shrinks and
  composites onto a grey plate any icon whose artwork doesn't fill the
  system's rounded-square grid (measured: 3/4 of what Finder drew for this
  app used to be that filler). `packaging/app_icon_macos.svg` is the true
  source (`resvg --width 1024 --height 1024 ... app_icon_macos.png`,
  byte-reproducible with resvg 0.48.1) — laid out on Apple's own template so
  the system draws it at full size instead of auto-shrinking it.
- **DMG**: built with `create-dmg` (not bare `hdiutil`, which can only drop
  the app + an `/Applications` alias into a default Finder window with no
  icon placement or background) — see `docs/AV_HARDENING.md` for why the
  final conversion step forces uncompressed UDRO.

## Windows-specific build notes

- **MSVC toolset pinned to one version** (`MSVC_TOOLSET` in `build.yml`)
  across both the AMD64 (`windows-latest`) and ARM64 runner images, so the
  two architectures link with identical mitigations/codegen — they'd
  otherwise silently resolve different toolsets (e.g. 14.51 vs 14.44) since
  the runner images don't ship the same set side by side.
- **The ARM64 runner label is not `windows-11-arm`** — it's pinned to
  `windows-11-vs2026-arm` because of an in-progress GitHub migration
  (`actions/runner-images#14602`, rollout window 2026-09-21 to
  2026-09-30): the floating label was briefly non-deterministic about which
  VS-based image it returned during that window. **Not verified**: whether
  the pinned `MSVC_TOOLSET` (14.44 at time of writing) actually exists
  side-by-side on the VS2026 image — VS2026 may not carry VS2022-era
  toolsets. If this job ever fails with "toolset not found," that's why;
  drop `MSVC_TOOLSET` to see what `setup-msvc-dev` reports as available, or
  check the runner-images repo's `Windows11-VS2026-Arm64-Readme.md`
  directly, then update both `MSVC_TOOLSET` and this note.
- Uses `TheMrMilchmann/setup-msvc-dev` (actively maintained fork on the
  node24 runtime), not `ilammy/msvc-dev-cmd` (unmaintained since Jan 2024,
  still on node20) — same `arch`/`toolset` inputs either way.
- **Console subsystem, not `WIN32_EXECUTABLE`**: the exe is linked with
  `/SUBSYSTEM:CONSOLE` so shells wait for the CLI modes, and
  `resources/app.manifest` (MSVC builds) sets `consoleAllocationPolicy` to
  `detached` so the GUI gets no console window on Windows 11 24H2+. The
  "Verify console subsystem + detached console policy" CI step checks both on
  the built exe. Rationale and consequences: `docs/KNOWN_ISSUES_AND_HISTORY.md`.
- **`.rc.in` → `.rc` expansion** (`resources/app.rc.in`) is Windows-only
  (the `.rc` format means nothing to other compilers); it embeds the app
  icon and `VERSIONINFO` block, both requiring `PROJECT_VERSION` to stay a
  strictly numeric `X.Y.Z` (the `" DEV"` suffix lives only in the separately
  generated `OPENMTR_VERSION` macro, never in `PROJECT_VERSION` itself).

## Debug info: generate, split, never simply absent

All three platforms follow the same pattern, for the same AV-hardening
reason (see `docs/AV_HARDENING.md`): Release build flags alone (`-O3
-DNDEBUG` / `/MD /O2 /Ob2 /DNDEBUG`) generate **no debug info structure at
all**, which itself reads as a mild "stripped/packed" signal to AV
heuristics — so debug info is always explicitly generated, then split out
into a companion file that's never actually shipped, leaving only the
normal marker a properly-built binary has:

| Platform | Generate | Split into | Marker left in shipped binary |
|---|---|---|---|
| Windows | `/Zi` + `/DEBUG:FULL` | `.pdb` (path scrubbed via `/PDBALTPATH`) | CodeView debug directory |
| macOS | `-g` | `.dSYM` (via `dsymutil`, next to the build tree, not inside `.app`) | `LC_UUID` + debug-map reference |
| Linux | `-g` | `.debug` (via `objcopy`, kept local) | `.gnu_debuglink` section |

Linux additionally forces `--build-id` explicitly rather than trusting
whatever the CI runner's linker defaults to — a missing
`.note.gnu.build-id` is itself an anomaly relative to virtually every
distro-packaged ELF.

## Test build (separate from the app build)

`tests/CMakeLists.txt` compiles `tracer.cpp` **in isolation** (no Qt, no
window) via `-DOPENMTR_BUILD_TESTS=ON` into its own build directory (`ctest
--test-dir build-tests`, kept entirely separate from the app's own `build/`
so the two never interfere). `AUTOMOC` is explicitly turned off for this
target since nothing in it is a Qt class. Currently run in CI on the macOS
job only. `engine_tests` reports genuinely-unavailable-environment cases
(no ICMP socket permission) as **skipped** via `SKIP_RETURN_CODE 77`, not
silently passed.

## Artifact shape (what CI uploads)

One zip per platform/arch, each containing a single plainly-named
executable — deliberately uniform:

```
OpenMTR-Windows-AMD64.zip / OpenMTR-Windows-ARM64.zip → OpenMTR.exe
OpenMTR-macOS.zip                                      → OpenMTR.dmg
OpenMTR-Linux-AMD64.zip  / OpenMTR-Linux-ARM64.zip     → OpenMTR.AppImage
```

Linux is packaged **only** as an AppImage — the raw unpackaged ELF is built
(it's literally the AppImage's payload) but never uploaded as its own
artifact, matching the "one zip, one executable" shape the other platforms
already have.

## AppImage assembly specifics

`out/bin/OpenMTR` still dynamically needs ordinary desktop libraries
(X11/xcb — including helper libs like `libxcb-cursor` not guaranteed present
everywhere — D-Bus, fontconfig, freetype). The build walks `ldd`'s output
and bundles every resolved dependency **except** the base glibc/toolchain
set every target is assumed to already have (`libc`, `ld-linux`,
`libpthread`, `libm`, `libdl`, `librt`, `libresolv`, `libgcc_s`) — mixing a
bundled libc with the host kernel's is what actually breaks portability, so
those specifically stay off the bundle. `AppRun` points
`LD_LIBRARY_PATH` at the bundled copies before exec'ing the real binary. The
`.desktop` file and icon must sit at the AppDir root per the AppImage spec
(not just under `usr/share/...`).
