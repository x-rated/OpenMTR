# OpenMTR

A modern, lightweight network diagnostic tool that combines traceroute and ping into a single real-time view. Built with a clean Qt6 interface, runs on Windows, macOS and Linux.

![OpenMTR Screenshot](https://i.imgur.com/fl7LcVm.jpg)
```
OpenMTR Export
Target  : gov.bw
Date    : 2026-07-08 00:32:51
Duration: 2:00

+-----+-------+-------------------------------------------------------------+-----------------+--------+------+------+---------+---------+---------+---------+---------+
| Hop | ASN   | Hostname                                                    | IP              | Loss % | Sent | Recv | Best ms | Avrg ms | Wrst ms | Last ms | Jttr ms |
+-----+-------+-------------------------------------------------------------+-----------------+--------+------+------+---------+---------+---------+---------+---------+
| 1   | -     | RT-BE88U                                                    | 192.168.1.1     | 0      | 119  | 119  | 0       | 0       | 0       | 0       | 0       |
| 2   | -     | 10.26.202.187                                               | 10.26.202.187   | 0      | 119  | 119  | 1       | 1       | 2       | 2       | 0       |
| 3   | 16019 | 217.77.171.154                                              | 217.77.171.154  | 96     | 25   | 1    | 1       | 1       | 1       | 1       | -       |
| 4   | 16019 | 217.77.171.153                                              | 217.77.171.153  | 18     | 68   | 56   | 2       | 2       | 3       | 3       | 0       |
| 5   | 16019 | 914.1-1-7.sitpe00.oskarmobil.cz                             | 217.77.160.49   | 26     | 59   | 44   | 2       | 3       | 4       | 3       | 0       |
| 6   | -     | Request timed out.                                                            | 100    | 24   | 0    | -       | -       | -       | -       | -       |
| 7   | -     | 20ge1-3.core1.prg1.he.net                                   | 91.210.16.201   | 0      | 119  | 119  | 4       | 5       | 51      | 5       | 2       |
| 8   | 6939  | 100ge0-0-0-7.core4.lon2.he.net                              | 184.105.213.13  | 17     | 72   | 60   | 22      | 23      | 25      | 23      | 0       |
| 9   | 6939  | be4.core2.lon3.he.net                                       | 184.104.192.53  | 0      | 119  | 119  | 46      | 47      | 50      | 49      | 0       |
| 10  | -     | Request timed out.                                                            | 100    | 24   | 0    | -       | -       | -       | -       | -       |
| 11  | 6939  | port-channel6.core1.lis1.he.net                             | 184.104.193.150 | 37     | 49   | 31   | 45      | 46      | 51      | 46      | 0       |
| 12  | 6939  | west-indian-ocean-cable-company-ltd.e0-27.core2.lis1.he.net | 184.104.204.94  | 0      | 119  | 119  | 45      | 45      | 72      | 46      | 1       |
| 13  | 37662 | 154.66.247.98                                               | 154.66.247.98   | 1      | 115  | 114  | 181     | 185     | 236     | 183     | 4       |
| 14  | 37662 | 154.66.247.93                                               | 154.66.247.93   | 0      | 119  | 119  | 184     | 185     | 202     | 185     | 0       |
| 15  | 37662 | 154.66.247.215                                              | 154.66.247.215  | 0      | 119  | 119  | 182     | 183     | 198     | 196     | 0       |
| 16  | 37662 | 154.66.247.236                                              | 154.66.247.236  | 0      | 119  | 119  | 181     | 184     | 237     | 185     | 4       |
| 17  | 37662 | 154.66.247.123                                              | 154.66.247.123  | 0      | 119  | 119  | 182     | 183     | 213     | 183     | 1       |
| 18  | 37662 | 102.68.115.249                                              | 102.68.115.249  | 0      | 119  | 119  | 184     | 184     | 186     | 185     | 0       |
| 19  | 37678 | 129.205.206.150                                             | 129.205.206.150 | 0      | 119  | 119  | 186     | 186     | 188     | 187     | 0       |
| 20  | 37678 | 129.205.195.134                                             | 129.205.195.134 | 0      | 119  | 119  | 188     | 189     | 192     | 190     | 0       |
| 21  | -     | Request timed out.                                                            | 100    | 24   | 0    | -       | -       | -       | -       | -       |
| 22  | -     | Request timed out.                                                            | 100    | 24   | 0    | -       | -       | -       | -       | -       |
| 23  | -     | Request timed out.                                                            | 100    | 24   | 0    | -       | -       | -       | -       | -       |
| 24  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
| 25  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
| 26  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
| 27  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
| 28  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
| 29  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
| 30  | -     | Request timed out.                                                            | 100    | 6    | 0    | -       | -       | -       | -       | -       |
+-----+-------+-------------------------------------------------------------+-----------------+--------+------+------+---------+---------+---------+---------+---------+

```

## Features

- **Real-time route tracing** — continuously probes every hop between you and the target, updating statistics live
- **Per-hop statistics** — ASN, hostname, IP address, packet loss, jitter, sent/received counts, best/avg/worst/last latency
- **Packet loss at a glance** — a per-hop visual bar shows loss severity, with a tooltip giving the exact numbers on hover; hops with 0% loss are dimmed so problem hops stand out
- **Route change / load-balancing detection** — hovering a hop that returned replies from more than one IP flags it as a route change or per-packet load balancing, with details in the tooltip
- **Hover tooltips everywhere in the table** — packet loss detail, and the full IP/hostname when it doesn't fit in its cell; tooltips track the cursor and stay fully on-screen
- **Live test duration** — a running timer in the title bar while a trace is active, frozen at its final value once stopped; start time and duration are also included in exported reports
- **ASN lookup** — Autonomous System Numbers resolved automatically via Team Cymru's DNS service
- **IPv4 & IPv6** — full dual-stack support with auto-fallback if the target can't be resolved with the chosen protocol
- **Configurable ping size** — adjust ICMP payload from 64 to 8192 bytes
- **Light & dark themes** — switches instantly and auto-detects the system theme on launch; the title bar follows along natively on every platform (DWM on Windows, Cocoa appearance on macOS, the desktop portal's accent/theme setting on Linux)
- **Custom frameless window** — the same Fluent-inspired look and controls on every platform; on macOS this includes a native application menu (About, Copy Report, Export…, Window)
- **Command-line report mode** — `openmtr-cli --count 10 1.1.1.1` (or `OpenMTR --report ...`) runs a trace without a window, prints the report (text or JSON) to stdout and exits with a meaningful exit code, for scripts, cron, SSH sessions and CI; installable with a `.deb` or a one-line script — see [Command-line report mode](#command-line-report-mode)
- **Export & copy** — save results as `.txt`, `.csv`, or `.json` via a native Save dialog, or copy the full report to clipboard; double-click any cell to copy its value; exported text adapts column widths to actual content
- **Keyboard shortcuts** — `Enter` in the target or ping size field starts/stops tracing; `Ctrl+C`/`⌘C` copies the full report to clipboard (or just the selected text when a text field is focused); `Ctrl+S`/`⌘S` opens the export dialog
- **Smart column sizing** — Hostname and IP columns dynamically share available space based on content width, and the toolbar itself adapts as the window narrows
- **No admin rights required** — runs as a standard user
- **Instant close** — the app exits immediately at any time; background threads are stopped asynchronously without blocking the UI
- **HiDPI aware** — crisp rendering on high-DPI and mixed-DPI setups on every platform

---

## Requirements

- Windows 11 (AMD64 or ARM64), macOS 13 Ventura or newer (Apple Silicon), or a Linux desktop (x86_64 or aarch64) with glibc 2.43 or newer

---

## Installing on macOS

Open the `.dmg` and drag **OpenMTR** to Applications.

The first launch is refused with a message that the app is damaged or cannot be
checked for malware, offering only **Move to Trash**. Nothing is wrong with the
download — the app is signed ad-hoc rather than with an Apple Developer ID, and
macOS blocks such apps outright once they carry the quarantine flag a browser
attaches. Unlike apps signed with a Developer ID but not notarised, there is no
**Open Anyway** button in *Privacy & Security* to fall back on.

Clear the quarantine flag once, after moving the app to Applications:

```sh
xattr -dr com.apple.quarantine /Applications/OpenMTR.app
```

It then launches normally, and the step is not needed again until you install a
new version. Removing this friction for good requires a paid Apple Developer
account so releases can be signed with a Developer ID and notarised.

macOS will also ask for **local network** access on the first trace. A
traceroute's first hop is normally your own router, so tracing anything needs
it.

---

## Command-line report mode

Like `mtr --report`, OpenMTR can run a trace without a window: it counts a
fixed number of probe cycles, prints the same report as **Copy** / **Export**
to stdout and exits. No display is needed, so it works over SSH, from cron and
in CI pipelines.

It comes in two forms with the same options and output:

- **`openmtr-cli`** — a small standalone console program. It needs no GUI
  libraries, so it also installs on servers, and on Windows it is a real
  console program that shells wait for.
- **`OpenMTR --report`** — the same mode built into the window app, for when
  the app is what you already have.

```sh
openmtr-cli --count 10 1.1.1.1
openmtr-cli --count 10 --json -6 example.com
OpenMTR --report --count 10 1.1.1.1
```

### Installing openmtr-cli

**Linux and macOS**, with the install script. It downloads the build for your
system from the latest release, checks its SHA-256 checksum and installs it to
`/usr/local/bin`, or to `~/.local/bin` if it cannot use `sudo`:

```sh
curl -fsSL https://raw.githubusercontent.com/x-rated/OpenMTR/main/install-cli.sh | sh
```

`OPENMTR_VERSION=<tag>` installs a specific release and `OPENMTR_BINDIR=<dir>`
picks the directory.

**Debian, Ubuntu and derivatives** can use the `.deb` package (amd64 or
arm64) instead, so the package manager keeps track of it:

```sh
curl -LO https://github.com/x-rated/OpenMTR/releases/latest/download/openmtr-cli-linux-amd64.deb
sudo apt install ./openmtr-cli-linux-amd64.deb
```

The Linux build is linked on Ubuntu 22.04 and needs only glibc 2.35 or newer
and libstdc++, so it runs on other distributions too: the install script (or
the plain `openmtr-cli-linux-<arch>.tar.gz`) covers those.

**Windows** — download `openmtr-cli.exe` (AMD64 or ARM64) from the release
page and put it on your `PATH`.

**macOS** needs Apple Silicon and macOS 13 or newer.

| Option | Meaning |
| --- | --- |
| `<target>` | Host name or IP address to trace |
| `-r`, `--report` | `OpenMTR`: run without a window (required). `openmtr-cli`: accepted for mtr compatibility, always on |
| `-c`, `--count <N>` | Probes to send to each hop that replies before reporting (default 10) |
| `-i`, `--interval <sec>` | Seconds between probes to each hop, 0.1–60 (default 1) |
| `-s`, `--size <bytes>` | ICMP payload size, 64–8192 bytes (default 64) |
| `-4` / `-6` | Use only IPv4 / only IPv6 (default: IPv4, falling back to IPv6) |
| `-n`, `--no-dns` | Do not resolve host names of hops |
| `--no-asn` | Do not look up AS numbers |
| `-j`, `--json` | Print the report as JSON (same fields as the JSON export, plus the run's settings and `destination_reached`) |
| `-h`, `--help` / `-v`, `--version` | Show the help / version and exit |

Before counting starts, OpenMTR waits a moment for the route to settle, as the
window does, so discovery probes are not counted. Counting then ends once every
hop that has replied has `--count` finished probes, every silent hop has at
least one, and at least one hop has `--count`. A lost probe only finishes once
it times out (after 5 s), and its hop waits for that before the next probe,
which has two consequences:

- Counting ends at the latest `--count` + 1 probe periods plus about 6 s after
  it starts. Hops that keep losing probes, or a route where no hop replies at
  all, run to that limit and can end with fewer than `--count` probes.
- With a small `--count` (under about 5 s of probing), hops that reply keep
  being probed while the first probes to silent hops time out, so they show
  more than `--count` in *Sent*.

Exit codes:

| Code | Meaning |
| --- | --- |
| 0 | Report printed; the destination replied |
| 1 | Report printed; the destination never replied during the run |
| 2 | Invalid command line |
| 3 | The target could not be resolved |
| 4 | The trace could not start (no ICMP socket) |
| 130 | Interrupted with Ctrl+C; the partial report is still printed |

Platform notes:

- **Windows** — prefer `openmtr-cli.exe` in scripts. `OpenMTR.exe` is a GUI
  program: batch files wait for it and see its exit code, but an interactive
  `cmd.exe` or PowerShell prompt does not, so its report appears after the
  prompt has already come back. To wait there, use
  `start /wait "" OpenMTR.exe --report 1.1.1.1` in `cmd.exe`, or pipe the
  output in PowerShell (`OpenMTR.exe --report 1.1.1.1 | Out-String`), which
  also sets `$LASTEXITCODE`. Piped output is UTF-8; if PowerShell shows
  non-ASCII characters garbled, run
  `[Console]::OutputEncoding = [Text.Encoding]::UTF8` first.
- **macOS** — the window app's report mode is the binary inside the bundle:
  `/Applications/OpenMTR.app/Contents/MacOS/OpenMTR --report 1.1.1.1`.
- **Linux** — OpenMTR uses unprivileged ICMP ("ping") sockets. If a
  distribution disables them, allow them with
  `sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"`. The AppImage
  itself needs FUSE (or `--appimage-extract-and-run` without it).
- ASN lookups use `dig` on macOS and Linux (the `.deb` recommends
  `bind9-dnsutils`); without it, the ASN column stays empty.

---

## Building

Releases are built automatically via GitHub Actions on every push to `main` — AMD64 and ARM64 binaries are produced in parallel and uploaded as artifacts. No local Qt installation is needed.

For a local build you need CMake 3.22+, Ninja, a C++20 compiler and Qt 6:

- **Windows** — Visual Studio 2022 and a static Qt 6 build.
- **macOS** — Qt 6 from Qt's official installer. Homebrew's `qt` also builds,
  but it is compiled for whichever macOS release the machine runs, so a bundle
  made with it will not start on older systems, and it links ICU, which adds
  ~35 MB to the app.
- **Linux** — the distribution's `qt6-base-dev` is enough to build and run. CI
  instead compiles Qt from source with `-no-icu`, purely to keep ICU out of the
  AppImage.

A build produces both programs, `OpenMTR` and `openmtr-cli`. `-DOPENMTR_BUILD_CLI=OFF`
skips the CLI; `-DOPENMTR_BUILD_GUI=OFF` builds only the CLI, which then needs
just QtCore (a Qt without GUI modules is enough). `packaging/package-cli.sh`
turns a built `openmtr-cli` into the release files (`.tar.gz`, `.deb`,
checksums).

The workflow in `.github/workflows/build.yml` documents the exact steps used in CI.

---

## Credits

OpenMTR is built on the shoulders of:

- **[WinMTR Redux](https://github.com/White-Tiger/WinMTR)** by White-Tiger — the network engine (IPv4/IPv6 ICMP tracing, per-hop statistics)
- **[WinMTR](https://github.com/WinMTR/WinMTR-Official)** by Vasile Laurentiu Stanimir (2000) — the original WinMTR
- **[BKPepe](https://github.com/BKPepe)** — created and fine-tuned the macOS build, with the help of Claude.ai

### AI disclosure

OpenMTR — the whole application, including this README — is built with [Claude](https://www.anthropic.com/claude) (Anthropic), directed and reviewed by the project maintainer.

---

## License

GPL v2 — see [LICENSE](LICENSE).

The network engine is derived from WinMTR Redux and original WinMTR, both GPL v2.

---

## Support

If OpenMTR has been useful to you, consider supporting its development — thank you! 💙

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/slamb)
[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-x--rated-EA4AAA?style=for-the-badge&logo=githubsponsors&logoColor=white)](https://github.com/sponsors/x-rated)
