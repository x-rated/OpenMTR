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
- **Update checker** — a quiet, one-shot check against GitHub Releases shortly after startup; a badge and a link appear only if a newer version is actually available
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
