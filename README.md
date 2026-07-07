# OpenMTR

A modern, lightweight network diagnostic tool for Windows that combines traceroute and ping into a single real-time view. Built with a clean Qt6 interface designed to feel native on Windows 11.

![OpenMTR Screenshot](https://i.imgur.com/1Qqgne8.jpg)
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
- **Light & dark themes** — switches instantly; Windows 11 title bar follows your choice via the native DWM API; auto-detects system theme on launch; separate light/dark app icons
- **Export & copy** — save results as `.txt`, `.csv`, or `.json` via a native Save dialog, or copy the full report to clipboard; double-click any cell to copy its value; exported text adapts column widths to actual content
- **Keyboard shortcuts** — `Enter` in the target or ping size field starts/stops tracing; `Ctrl+C` copies the full report to clipboard (or just the selected text when a text field is focused); `Ctrl+S` opens the export dialog
- **Smart column sizing** — Hostname and IP columns dynamically share available space based on content width
- **Fluent Design UI** — interface fine-tuned to match the WinUI 3 / Fluent Design look and feel (Mica backdrop, spacing, borders, controls, focus states)
- **No admin rights required** — runs as a standard user
- **Instant close** — the app exits immediately at any time; background threads are stopped asynchronously without blocking the UI
- **HiDPI aware** — Per-Monitor V2 DPI aware for crisp rendering on high-DPI and mixed-DPI setups

---

## Requirements

- Windows 11
- AMD64 or ARM64 processor

---

## Building

Releases are built automatically via GitHub Actions on every push to `main` — AMD64 and ARM64 binaries are produced in parallel and uploaded as artifacts. No local Qt installation is needed.

For a local build you need Visual Studio 2022 (C++20), CMake 3.22+, Ninja, and a static Qt 6 build. The workflow in `.github/workflows/build.yml` documents the exact steps used in CI.

---

## Credits

OpenMTR is built on the shoulders of:

- **[WinMTR Redux](https://github.com/White-Tiger/WinMTR)** by White-Tiger — the network engine (IPv4/IPv6 ICMP tracing, per-hop statistics)
- **[WinMTR](https://github.com/WinMTR/WinMTR-Official)** by Vasile Laurentiu Stanimir (2000) — the original WinMTR

---

## License

GPL v2 — see [LICENSE](LICENSE).

The network engine is derived from WinMTR Redux and original WinMTR, both GPL v2.
