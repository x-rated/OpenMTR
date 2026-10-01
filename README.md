<div align="center">

# OpenMTR

**Traceroute and ping in a single real-time view.**

A modern, lightweight network diagnostic tool with a clean Qt6 interface —
for Windows, macOS and Linux.

🌐 **[openmtr.net](https://www.openmtr.net/)**

![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-2f81f7?style=flat-square)
![Qt](https://img.shields.io/badge/Qt-6-41cd52?style=flat-square&logo=qt&logoColor=white)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599c?style=flat-square&logo=cplusplus&logoColor=white)
![License: GPL v2](https://img.shields.io/badge/license-GPL%20v2-blue?style=flat-square)

[Website](https://www.openmtr.net/) ·
[Features](#features) ·
[Requirements](#requirements) ·
[Command-line mode](#command-line-report-mode) ·
[Installing on macOS](#installing-on-macos) ·
[Building](#building) ·
[Credits](#credits)

<br>

![OpenMTR Screenshot](https://i.imgur.com/tz8QFi8.jpg)

</div>

---

## Why OpenMTR?

| Highlight | What it means |
|---|---|
| ⚡ **Live** | Every hop between you and the target is probed continuously and the statistics update in real time. |
| 🔍 **Problem hops stand out** | Loss bars and dimmed 0 % hops make trouble spots obvious at a glance. |
| 🖥️ **GUI or CLI** | Full desktop app, plus a headless report mode for scripts, cron jobs, CI and SSH. |
| 🔓 **No admin rights** | Runs as a standard user. |

<details>
<summary><b>📄 Example exported report</b> (click to expand)</summary>
<br>

```
OpenMTR Export
Target  : www.wide.ad.jp
Date    : 2026-09-29 08:41:48
Duration: 2:00

+-----+-------+-------------------------------------+-----------------+--------+------+------+---------+---------+---------+---------+---------+
| Hop |   ASN | Hostname                            | IP              | Loss % | Sent | Recv | Best ms | Avrg ms | Wrst ms | Last ms | Jttr ms |
+-----+-------+-------------------------------------+-----------------+--------+------+------+---------+---------+---------+---------+---------+
|   1 |     - | RT-BE88U                            | 192.168.1.1     |      0 |  119 |  119 |       0 |       0 |       1 |       0 |       0 |
|   2 |     - | 10.26.202.187                       | 10.26.202.187   |      0 |  119 |  119 |       1 |       1 |       2 |       1 |       0 |
|   3 |     - | Request timed out.                                    |    100 |   24 |    0 |       - |       - |       - |       - |       - |
|   4 | 16019 | 217.77.171.153                      | 217.77.171.153  |     35 |   52 |   34 |       2 |       2 |       3 |       3 |       0 |
|   5 | 16019 | ispr00i-nixr00i-0.oskarmobil.cz     | 217.77.160.49   |     89 |   26 |    3 |       3 |       3 |       3 |       3 |       0 |
|   6 |     - | Request timed out.                                    |    100 |   24 |    0 |       - |       - |       - |       - |       - |
|   7 |  1273 | ae29-100-ucr1.pra.cw.net            | 195.2.12.33     |      0 |  118 |  118 |       2 |       3 |       4 |       4 |       0 |
|   8 |  1273 | ae3-ucr1.czs.cw.net                 | 195.2.18.249    |      0 |  118 |  118 |     222 |     222 |     223 |     222 |       0 |
|   9 |  1273 | ae40-pcr1.fnt.cw.net                | 195.2.10.234    |      0 |  118 |  118 |     222 |     223 |     242 |     232 |       1 |
|  10 |  1273 | ae19-xcr2.fri.cw.net                | 195.2.16.34     |      0 |  118 |  118 |     222 |     222 |     239 |     222 |       0 |
|  11 |  1273 | ae33-xcr1.hkg.cw.net                | 195.2.8.37      |      0 |  118 |  118 |     173 |     173 |     177 |     173 |       0 |
|  12 |  1273 | ae2-xcr1.tyo.cw.net                 | 195.2.10.18     |      0 |  119 |  119 |     222 |     223 |     244 |     222 |       1 |
|  13 | 17676 | softbank221111202165.bbtec.net      | 221.111.202.165 |      0 |  119 |  119 |     222 |     224 |     256 |     223 |       2 |
|  14 |     - | Request timed out.                                    |    100 |   24 |    0 |       - |       - |       - |       - |       - |
|  15 |     - | 101.203.107.46                      | 101.203.107.46  |      1 |  115 |  114 |     223 |     226 |     275 |     224 |       3 |
|  16 |  2500 | ve113.juniper1.notemachi.wide.ad.jp | 203.178.136.118 |      0 |  119 |  119 |     224 |     224 |     228 |     224 |       0 |
|  17 |  2500 | ve61.juniper1.otemachi.wide.ad.jp   | 203.178.136.117 |      0 |  119 |  119 |     224 |     224 |     239 |     225 |       1 |
|  18 |  2500 | www.wide.ad.jp                      | 203.178.139.57  |      0 |  119 |  119 |     223 |     224 |     225 |     224 |       0 |
+-----+-------+-------------------------------------+-----------------+--------+------+------+---------+---------+---------+---------+---------+
```

</details>

---

## Features

### 📡 Tracing & statistics

- **Real-time route tracing** — continuously probes every hop between you and the target, updating statistics live
- **Per-hop statistics** — ASN, hostname, IP address, packet loss, jitter, sent/received counts, best/avg/worst/last latency
- **Packet loss at a glance** — a per-hop visual bar shows loss severity, with a tooltip giving the exact numbers on hover; hops with 0% loss are dimmed so problem hops stand out
- **Route change / load-balancing detection** — hovering a hop that returned replies from more than one IP flags it as a route change or per-packet load balancing, with details in the tooltip
- **ASN lookup** — Autonomous System Numbers resolved automatically via Team Cymru's DNS service
- **IPv4 & IPv6** — full dual-stack support with auto-fallback if the target can't be resolved with the chosen protocol
- **Configurable ping size** — adjust ICMP payload from 64 to 8192 bytes
- **Live test duration** — a running timer in the title bar while a trace is active, frozen at its final value once stopped; start time and duration are also included in exported reports

### 🎨 Interface

- **Light & dark themes** — switches instantly and auto-detects the system theme on launch; the title bar follows along natively on every platform (DWM on Windows, Cocoa appearance on macOS, the desktop portal's accent/theme setting on Linux)
- **Custom frameless window** — the same Fluent-inspired look and controls on every platform; on macOS this includes a native application menu (About, Copy Report, Export…, Window)
- **Hover tooltips everywhere in the table** — packet loss detail, and the full IP/hostname when it doesn't fit in its cell; tooltips track the cursor and stay fully on-screen
- **Smart column sizing** — Hostname and IP columns dynamically share available space based on content width, and the toolbar itself adapts as the window narrows
- **HiDPI aware** — crisp rendering on high-DPI and mixed-DPI setups on every platform

### 📤 Export & workflow

- **Export & copy** — save results as `.txt`, `.csv`, or `.json` via a native Save dialog, or copy the full report to clipboard; double-click any cell to copy its value; exported text adapts column widths to actual content
- **Headless report mode** — `OpenMTR --report --time <seconds> <target>` runs a one-shot trace from the command line (text, JSON or CSV), no window needed — see [Command-line report mode](#command-line-report-mode)

### 🚀 Performance & convenience

- **No admin rights required** — runs as a standard user
- **Instant close** — the app exits immediately at any time; background threads are stopped asynchronously without blocking the UI

### ⌨️ Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Enter` (in the target or ping size field) | Start / stop tracing |
| `Ctrl+C` / `⌘C` | Copy the full report to clipboard — or just the selected text when a text field is focused |
| `Ctrl+S` / `⌘S` | Open the export dialog |
| Double-click a cell | Copy that cell's value |

---

## Requirements

| Platform | Supported versions |
|---|---|
| **Windows** | Windows 11, AMD64 or ARM64. 24H2 or newer recommended — on earlier builds the window opens with an empty console window behind it, see [the Windows note](#command-line-report-mode) below. |
| **macOS** | macOS 14.4 Sonoma or newer (Apple Silicon) |
| **Linux** | Desktop Linux, x86_64 or aarch64, with glibc 2.43 or newer |

---

## Command-line report mode

Besides the GUI, OpenMTR can run a headless one-shot trace from the command
line and print a report — no window, no display needed. Useful for scripts,
cron jobs, CI, or a quick check over SSH.

```sh
OpenMTR --report --time <seconds> [options] <target>
```

### Options

| Option | Meaning |
|---|---|
| `--time <seconds>` | **Required.** How long to measure *after* the route has settled (warm-up time isn't counted). |
| `--size <bytes>` | ICMP payload size, 64–8192 (default: 64 — same as the window). |
| `-4` / `-6` | Force IPv4 or IPv6. Default is IPv6 with automatic IPv4 fallback if the target has no AAAA record — same as the window's IPv6 checkbox. |
| `--txt` | Print the plain-text report (the default; an explicit form of it exists alongside `--json`/`--csv` for scripting clarity). |
| `--json` | Print the JSON report instead of text. |
| `--csv` | Print the report as CSV instead of text. |
| `--output <path>` | Write the report directly to this file instead of stdout. Works with no shell or console at all (Task Scheduler, a service), and is otherwise equivalent to `>`. |
| `-h`, `--help` | Show usage. |
| `-v`, `--version` | Print the version number and exit — works with or without `--report`. |

Everything else (ASN lookup, reverse DNS, per-hop statistics, the warm-up
that keeps a slow-to-discover route from skewing the numbers) works exactly
as it does in the window — the report is built from the same code either
way, so a target that behaves one way in the GUI behaves the same way here.

### Examples

```sh
# Text report, 30 seconds, after the route settles
OpenMTR --report --time 30 example.com

# JSON, forced IPv4, straight to a file
OpenMTR --report --time 30 --json -4 --output report.json example.com

# CSV
OpenMTR --report --time 30 --csv --output report.csv example.com
```

A progress line ("Discovering route...", "Measuring... 12s remaining") is
shown on stderr while a real terminal is attached, and suppressed
automatically when output is redirected to a file or a log, so it never ends
up mixed into a saved report.

### Exit codes

For scripting:

| Code | Meaning |
|:---:|---|
| `0` | Report printed, the destination replied. |
| `1` | Report printed, the destination never replied. |
| `2` | Invalid command line. |
| `3` | The target could not be resolved. |
| `4` | The trace could not start (no ICMP socket/handle). |
| `5` | `--output`'s file could not be written. |
| `130` | Interrupted with Ctrl+C; the partial report is still printed. |

### Notes for Windows

OpenMTR is a console-subsystem program (and, on Windows 11 24H2 or newer,
opens no console window of its own when started from Explorer or the Start
menu). In cmd.exe and PowerShell it therefore behaves like any other command:
the shell waits for it, the report and progress line appear in the terminal,
the next prompt is drawn by the shell afterwards, `$LASTEXITCODE` /
`%ERRORLEVEL%` carry the exit codes above, and `>`, `| Out-File` and
`--output` all work.

Two things follow from that, both needing to be known:

> [!IMPORTANT]
> **Windows 11 24H2 (build 26100) or newer** is what keeps the window free of
> a console. Earlier versions don't understand the manifest setting that
> suppresses it, so starting the window from Explorer also shows an empty
> console window behind it. The command-line mode is unaffected.

> [!TIP]
> **Started from a shell without arguments**, e.g. `.\OpenMTR.exe`, the shell
> waits until the window is closed, just as with any console program. Use
> `start OpenMTR.exe` (cmd) or `Start-Process OpenMTR.exe` (PowerShell) to
> open the window and get the prompt back at once.

> [!NOTE]
> Run with no console and no redirection at all (Task Scheduler and the like),
> `--output` is the only way to get the report — there's otherwise nowhere for
> the output to go.

---

## Installing on macOS

Open the `.dmg` and drag **OpenMTR** to Applications.

> [!WARNING]
> The first launch is refused with a message that the app is damaged or cannot be
> checked for malware, offering only **Move to Trash**. Nothing is wrong with the
> download — the app is signed ad-hoc rather than with an Apple Developer ID, and
> macOS blocks such apps outright once they carry the quarantine flag a browser
> attaches. Unlike apps signed with a Developer ID but not notarised, there is no
> **Open Anyway** button in *Privacy & Security* to fall back on.

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

## Building

Releases are built automatically via GitHub Actions on every push to `main` — AMD64 and ARM64 binaries are produced in parallel and uploaded as artifacts. No local Qt installation is needed.

For a local build you need **CMake 3.25+**, **Ninja**, a **C++20 compiler** and **Qt 6.12 or newer**:

| Platform | What you need |
|---|---|
| **Windows** | Visual Studio 2022 and a static Qt 6 build. |
| **macOS** | Qt 6 from Qt's official installer. Homebrew's `qt` also builds, but it is compiled for whichever macOS release the machine runs, so a bundle made with it will not start on older systems, and it links ICU, which adds ~35 MB to the app. |
| **Linux** | The distribution's `qt6-base-dev` is enough to build and run, provided it is Qt 6.12 or newer; otherwise build Qt from source. CI instead compiles Qt from source with `-no-icu`, purely to keep ICU out of the AppImage. |

The workflow in [`.github/workflows/build.yml`](.github/workflows/build.yml) documents the exact steps used in CI.

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

<div align="center">

## Support

If OpenMTR has been useful to you, consider supporting its development — thank you! 💙

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/slamb)
[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-x--rated-EA4AAA?style=for-the-badge&logo=githubsponsors&logoColor=white)](https://github.com/sponsors/x-rated)

</div>
