# OpenMTR

A modern, lightweight network diagnostic tool for Windows that combines traceroute and ping into a single real-time view. Built with a clean Qt6 interface designed to feel native on Windows 11.

![OpenMTR Screenshot](https://i.imgur.com/uIyV0Vy.jpg)
```
OpenMTR Export
Target  : gov.bw
Date    : 2026-03-20 00:28:43

+-------+---------+---------------------------------------------------------------+-------------------+---------+--------+--------+--------+--------+--------+--------+
| Hop   | ASN     | Hostname                                                      | IP                | Loss %  | Sent   | Recv   | Best m | Avrg m | Wrst m | Last m |
+-------+---------+---------------------------------------------------------------+-------------------+---------+--------+--------+--------+--------+--------+--------+
| 1     | -       | RT-BE88U                                                      | 192.168.1.1       | 0       | 100    | 100    | 0      | 0      | 2      | 0      |
| 2     | -       | 10.26.202.187                                                 | 10.26.202.187     | 0       | 100    | 100    | 1      | 1      | 3      | 2      |
| 3     | 16019   | 217.77.171.154                                                | 217.77.171.154    | 98      | 101    | 3      | 2      | 2      | 2      | 2      |
| 4     | 16019   | 217.77.171.153                                                | 217.77.171.153    | 11      | 101    | 90     | 2      | 3      | 6      | 3      |
| 5     | 16019   | ispr00i-nixr00i-0.oskarmobil.cz                               | 217.77.160.49     | 33      | 101    | 68     | 2      | 3      | 5      | 3      |
| 6     | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 7     | -       | 20ge1-3.core1.prg1.he.net                                     | 91.210.16.201     | 0       | 101    | 101    | 4      | 5      | 17     | 6      |
| 8     | 6939    | 100ge0-65.core3.fra1.he.net                                   | 184.105.213.233   | 78      | 101    | 23     | 11     | 37     | 290    | 35     |
| 9     | 6939    | be47.core4.fra1.he.net                                        | 184.105.213.207   | 0       | 101    | 101    | 11     | 12     | 18     | 12     |
| 10    | 6939    | be1.core3.par2.he.net                                         | 72.52.92.157      | 7       | 101    | 94     | 42     | 43     | 46     | 45     |
| 11    | -       | ?                                                             | ?                 | 100     | 102    | 0      | -      | -      | -      | -      |
| 12    | -       | ?                                                             | ?                 | 100     | 102    | 0      | -      | -      | -      | -      |
| 13    | 6939    | port-channel6.core1.lis1.he.net                               | 184.104.193.150   | 30      | 102    | 72     | 43     | 43     | 47     | 43     |
| 14    | 6939    | west-indian-ocean-cable-company-ltd.e0-27.core2.lis1.he.net   | 184.104.204.94    | 0       | 100    | 100    | 43     | 43     | 58     | 43     |
| 15    | 37662   | 154.66.247.98                                                 | 154.66.247.98     | 0       | 100    | 100    | 170    | 171    | 183    | 171    |
| 16    | 37662   | 154.66.247.107                                                | 154.66.247.107    | 0       | 100    | 100    | 156    | 157    | 178    | 158    |
| 17    | 37662   | 154.66.247.67                                                 | 154.66.247.67     | 0       | 100    | 100    | 173    | 174    | 185    | 174    |
| 18    | 37662   | 102.68.115.249                                                | 102.68.115.249    | 0       | 100    | 100    | 172    | 173    | 188    | 173    |
| 19    | 37678   | 129.205.206.150                                               | 129.205.206.150   | 0       | 100    | 100    | 177    | 177    | 178    | 177    |
| 20    | 37678   | 129.205.195.134                                               | 129.205.195.134   | 0       | 100    | 100    | 176    | 177    | 188    | 177    |
| 21    | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 22    | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 23    | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 24    | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 25    | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 26    | -       | ?                                                             | ?                 | 100     | 101    | 0      | -      | -      | -      | -      |
| 27    | -       | ?                                                             | ?                 | 100     | 102    | 0      | -      | -      | -      | -      |
| 28    | -       | ?                                                             | ?                 | 100     | 102    | 0      | -      | -      | -      | -      |
| 29    | -       | ?                                                             | ?                 | 100     | 102    | 0      | -      | -      | -      | -      |
| 30    | -       | ?                                                             | ?                 | 100     | 102    | 0      | -      | -      | -      | -      |
+-------+---------+---------------------------------------------------------------+-------------------+---------+--------+--------+--------+--------+--------+--------+
```

## Features

- **Real-time route tracing** — continuously probes every hop between you and the target, updating statistics live
- **Per-hop statistics** — ASN, hostname, IP address, packet loss, sent/received counts, best/avg/worst/last latency
- **ASN lookup** — Autonomous System Numbers resolved automatically via Team Cymru's DNS service
- **IPv4 & IPv6** — full dual-stack support with auto-fallback if the target can't be resolved with the chosen protocol
- **Configurable ping size** — adjust ICMP payload from 64 to 8192 bytes
- **Light & dark themes** — switches instantly; Windows 11 title bar follows your choice via the native DWM API; auto-detects system theme on launch
- **Export & copy** — save results as `.txt` or `.csv`, or copy the full report to clipboard; double-click any cell to copy its value; exported text adapts column widths to actual content
- **Smart column sizing** — Hostname and IP columns dynamically share available space based on content width
- **No admin rights required** — runs as a standard user
- **Instant close** — the app exits immediately at any time; background threads are stopped asynchronously without blocking the UI
- **HiDPI aware** — Per-Monitor V2 DPI aware for crisp rendering on high-DPI and mixed-DPI setups

<br>

> [!WARNING]
> **OpenMTR is not code signed.** Free signing services ([SignPath Foundation](https://signpath.org) and [OSSign](https://ossign.org)) require a minimum project history of 6 months before accepting new projects.
>
> As a result, some antivirus engines may flag the executable as suspicious due to [Enigma Virtual Box](https://enigmaprotector.com/en/aboutvb.html) packing, which is used to bundle Qt6 runtime DLLs into a single portable executable. This is a false positive — the source code is fully available for review at this repository, and the build is fully reproducible via the [GitHub Actions workflow](.github/workflows/build.yml).

---

## Requirements

- Windows 10 22H2+ (Windows 11 recommended for full Mica/DWM effects)
- Qt 6.11+ with `msvc2022_64` kit
- Visual Studio 2022 with C++20 support

---

## Building

1. Install [Qt 6.11+](https://www.qt.io/download) with the `msvc2022_64` component
2. If not using the `QT_ROOT_DIR` environment variable, update `QtDir` in `OpenMTR.vcxproj` to match your Qt installation path
3. Open `OpenMTR.sln` in Visual Studio 2022
4. Build → **Release x64**

Releases are also built automatically via GitHub Actions on every push.

---

## Credits

OpenMTR is built on the shoulders of:

- **[WinMTR Redux](https://github.com/White-Tiger/WinMTR)** by White-Tiger — the network engine (IPv4/IPv6 ICMP tracing, per-hop statistics)
- **[WinMTR](http://winmtr.net/)** by Vasile Laurentiu Stanimir / [Appnor MSP](http://www.appnor.com) (2000) — the original WinMTR

---

## License

GPL v2 — see [LICENSE](LICENSE).

The network engine is derived from WinMTR Redux (White-Tiger) and original WinMTR (Appnor MSP), both GPL v2.
