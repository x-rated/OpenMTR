# Antivirus / ML-Heuristic False-Positive Hardening

**Read this before "cleaning up" anything that looks unusual in
`CMakeLists.txt`, `updatecheck.cpp`, or the packaging steps of
`.github/workflows/build.yml`.** Several choices here look like odd
overengineering in isolation but are direct responses to real, confirmed
false-positive detections (Microsoft's Wacatac ML classifier, seen on
VirusTotal) against previous shipped builds. Undoing any one of them without
understanding why can silently reintroduce a false positive.

This is not about being actually malicious or actually safe — the app is
neither more nor less safe for any of this. It's entirely about not
*resembling* malware to opaque ML classifiers that weight structural
signals like "no TLS mitigation," "high-entropy compressed sections," and
"no debug info."

## The core insight: entropy and network-stack signals are the two levers

1. **A statically-linked TLS/network stack that phones home** reads like
   downloader/dropper behavior to ML classifiers, independent of what it
   actually does.
2. **High-entropy (compressed) binary sections** read the same as a
   packed/obfuscated payload to a scanner, independent of whether the
   compression is entirely legitimate (a normal AppImage squashfs or DMG
   image, for instance).

Every mitigation below is one of these two levers.

## No Qt6Network, anywhere, ever

The only network call outside the tracing engine itself — the GitHub
release check — shells out to the OS's own `curl` (`updatecheck.cpp`)
instead of using Qt6Network in-process. Rationale, verbatim from the code:
statically linking Qt6Network purely for a version check gives the Windows
binary a network+TLS profile AV/ML heuristics associate with downloader/C2
behavior, and this **has** triggered false positives before. Routing through
`curl.exe`/`/usr/bin/curl` — binaries AV/EDR vendors already allowlist —
means the app binary itself never performs a TLS handshake or carries a
network stack at all. `Qt6Network` is not linked by this project, full stop
(`CMakeLists.txt` ~L54).

Rules that follow from this, if you ever touch `updatecheck.cpp`:
- `curlPath` is always a **hardcoded absolute path** (`C:/Windows/System32/curl.exe`
  on Windows, `/usr/bin/curl` on macOS, resolved once via
  `QStandardPaths::findExecutable` on Linux) — never a bare `"curl"` string
  handed to `QProcess`'s own PATH search. Unqualified PATH lookup is
  platform-inconsistent and has had a real security advisory against it.
- `QProcess::start()` (argv array) is used, never `startCommand()` or a
  shell — no shell-quoting/injection surface, even though every argument
  here is currently a compile-time literal.
- The entire feature is gated by **one** compile-time switch,
  `OPENMTR_ENABLE_UPDATE_CHECK`, set in `CMakeLists.txt` and read by every
  call site (`MainWindow.cpp`, `cli.cpp`) via `updatecheck.h`. Currently
  `0` (compiled out entirely, not just unreachable — the curl/subprocess/
  GitHub-API code and string literals are absent from the binary). Flip
  the one CMake line to re-enable everywhere at once; there is deliberately
  no way for it to be on in one front end and off in the other.

## Windows PE hardening flags (`CMakeLists.txt` ~L340-390)

- **`/guard:cf` + `/GUARD:CF`** (Control Flow Guard): standard exploit
  mitigation, and its presence sets a DLL-characteristics bit that's a
  strong "benign" signal to ML classifiers (malware rarely ships CFG). CI
  (`build.yml` ~L188) independently re-verifies the built `.exe` actually
  carries the guard tables via `dumpbin /loadconfig` — **not** by grepping
  for the string "Guard Control Flow Guard" (a CFG binary's characteristics
  just say the bare word "Guard"; the load-config check confirms the actual
  tables are present, since a linker/toolset mismatch on a cross-compiled
  target can silently drop the bit even with the right flags). Fails the
  build loudly rather than shipping a binary assumed-hardened but wasn't.
- **PE subsystem is console (CUI), not GUI**: changed so that shells wait for
  the CLI modes (see `docs/KNOWN_ISSUES_AND_HISTORY.md`). Its effect on AV
  classifiers has **not** been assessed either way; if a new false-positive
  report appears right after this change, it is a candidate to check first.
- **`/RELEASE`**: forces the linker to compute a correct PE header checksum.
  A lot of malware ships with a missing/invalid checksum; a correct one is a
  mild "properly built" signal.
- **`/GL` + `/LTCG`** (whole-program optimization): standard for
  professionally built release binaries; its absence can itself read as an
  "amateur toolchain" signal.
- **Delay-loaded networking DLLs** (`ws2_32`, `Iphlpapi`, `dnsapi`): loaded
  only when code actually calls into them (first ping / DNS / ASN lookup)
  instead of appearing as unconditional startup dependencies in the import
  table — some AV ML classifiers weight "reaches for the network
  immediately at startup" as a signal.
- **Debug info is generated, then deliberately split out, never entirely
  absent**: `/Zi` + `/DEBUG:FULL` force a real CodeView debug directory to
  exist in the shipped `.exe` (CMake's own Release flags generate *none* at
  all otherwise) — a Windows binary with **zero** debug-directory presence
  is itself a mild "packed/stripped" signal, even though the matching
  `.pdb` is never actually distributed (`/PDBALTPATH` scrubs the embedded
  absolute build-machine path down to a bare filename first). `/DEBUG:FULL`
  (not the default `/DEBUG:FASTLINK`) is required because `/GL`+`/LTCG`
  already merges everything into one compiland; the corresponding
  `/OPT:REF /OPT:ICF` are set explicitly because `/DEBUG:FULL` alone makes
  the linker more conservative about folding duplicate code (measured: this
  grew the binary from ~16MB to ~23MB before `/OPT:REF`/`/OPT:ICF` were
  added back — comparing section sizes confirmed `.text`/`.rdata` accounted
  for essentially all of the growth). The same "generate then split, never
  simply absent" pattern is mirrored on macOS (`dsymutil` → companion
  `.dSYM`, `LC_UUID`+debug-map reference left in the Mach-O) and Linux
  (`objcopy` → companion `.debug` file + `.gnu_debuglink` section) — see
  `docs/BUILD_AND_CI.md`.

## Packaging entropy (DMG and AppImage — do not re-enable compression)

Both the macOS `.dmg` and the Linux `.AppImage` had **confirmed** Wacatac
false positives from compressed payloads reading as high-entropy blobs to a
scanner — fixed identically on both:

- **macOS DMG** (`build.yml` ~L383-410): `create-dmg` only offers compressed
  formats, so the polished (background/icon-positioned) image is built
  under a temp name, then `hdiutil convert`-ed to **UDRO (fully
  uncompressed)** for the actual shipped artifact — not just a "looser"
  compressor; the comment is explicit that *any* compressed format's data
  fork still reads close to random-entropy to a scanner (ULFO/lzfse was
  tried as a middle ground and still isn't the fix; only uncompressed gets
  the entropy profile down near the statically-linked `.exe`'s ~6.5 instead
  of a compressed blob's ~8.0).
- **Linux AppImage** (`build.yml` ~L584-625): a normal AppImage's squashfs
  payload is zstd-compressed by default — same entropy problem, same fix.
  `mksquashfs -noI -noD -noF -noX` disables inode/data/fragment/xattr
  compression so the payload stores the binary's bytes close to as-is.

**If either build ever starts tripping AV again**, the next lever (per the
code's own note) is **code signing** — an unsigned binary is itself a
heavily-weighted signal for these classifiers on every platform,
independent of container format — deferred so far due to cost, not
technical difficulty.

## What this does *not* guarantee

Wacatac.B!ml/C!ml (and similar) are opaque ML classifiers, not documented
rules. Every fix here removes one *concrete, previously confirmed* trigger.
None of it — individually or combined — can *guarantee* some future AV
engine won't flag a given binary. Treat a new false-positive report as
"find the next concrete signal," not "our hardening failed."
