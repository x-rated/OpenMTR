# Engine Internals (`tracer.h` / `tracer.cpp`)

The core traceroute+ping engine. Two layers:

- **`OpenMTRNet`** — low-level engine. `DoTrace()` runs a single async
  dispatch loop that drives ICMP probes for every TTL. Results live in a
  fixed hop table guarded by `m_mutex`.
- **`OpenMTRNetWrapper`** — thread-friendly facade. Owns an `OpenMTRNet` on a
  background thread, hands out immutable per-hop snapshots. This is what
  MainWindow and cli.cpp both actually talk to.

## The dispatch loop, not one-thread-per-hop

`DoTrace()` drives all `MAX_HOPS` probes from **one** loop instead of one
blocking OS thread per TTL — deliberately, to avoid a thread's stack + kernel
scheduling slot per hop. Each hop still gets its own OS-level probe handle
(Windows: its own `HANDLE hICMP`; POSIX: its own socket per trace) — that's
for wire-level identification (see below), not concurrency.

- **Windows** (`tracer.cpp` ~L279-570): `IcmpSendEcho2()`/`Icmp6SendEcho2()`
  submitted with a per-hop `Event` HANDLE instead of blocking. The dispatcher
  sleeps in `WaitForMultipleObjects()` on every outstanding hop's event plus
  a stop event — never busy-polls. Relies on documented IP Helper API
  behavior: a non-NULL Event makes the call return immediately
  (`GetLastError() == ERROR_IO_PENDING`) and signals the event once a
  reply/timeout completes, reply buffer already filled in.
- **POSIX** (`tracer.cpp` ~L575-1145): same per-hop scheduling/parking logic,
  built on unprivileged ping sockets (`SOCK_DGRAM`/`IPPROTO_ICMP[V6]`, no
  root needed) and `poll()` instead of `WaitForMultipleObjects`.

Per-hop scheduling: absolute schedule `t0 + slot*period`, evaluated by the
dispatcher rather than each hop sleeping on its own stack. Hop *i*'s initial
stagger is `i * 50ms` so probes spread across the interval instead of firing
on the same tick. **Period is `interval + 16ms`**, not exactly `interval` —
ICMP rate limiters commonly refill on a whole-second cycle; probing at
exactly that period would pin every arrival to one fixed phase of the
limiter's clock and a hop parked at the refill boundary would drop replies
rhythmically. The extra 16ms sweeps the phase across the limiter's whole
cycle roughly once per 64 probes.

**Parking**: once loss counting starts (`ResetStats()` enables it), probes
more than `UNKNOWN_HOP_MARGIN` (3) TTLs past the farthest hop that ever
answered get throttled to one patrol probe every `UNKNOWN_PATROL_MS` instead
of full rate — such probes cross the whole path only to die unanswered, and
at full rate they'd feed rate limiters on every router along the way,
degrading the useful measurements.

## Per-platform ICMP quirks — read this before touching packet handling

This is the single most fragile, most-tested-empirically part of the
codebase. Do not change wire-format or receive-path code without
understanding why each platform branch exists.

| Concern | Windows | Linux | macOS/BSD |
|---|---|---|---|
| API | `IcmpSendEcho2`/`Icmp6SendEcho2` | raw sendto/recvfrom on unprivileged ping socket | same as Linux |
| Intermediate-hop errors (Time Exceeded, Unreachable) | in the reply buffer, same call | **socket error queue** (`IP_RECVERR`/`IPV6_RECVERR`, drained via `POLLERR`) — a plain `recvfrom()` never sees them | delivered to ordinary `recvfrom()`, no error queue needed |
| Per-socket demux by echo id | N/A (per-hop handle) | kernel rewrites id to its own "port" and only delivers matching traffic | **no demultiplexing at all** — every open ICMP socket gets a copy of every inbound message; must filter by sequence number ourselves |
| Receive buffer | N/A | default | `net.inet.raw.recvspace` defaults to **8KB** — an echo reply ≥ ~8133 bytes doesn't fit and silently drops (probes at large `--size` all "time out"); explicitly set `SO_RCVBUF` to 64KB |
| Leading IP header on read | N/A | absent | present on some BSD kernels incl. macOS — stripped before reading the ICMP header at a fixed offset |

**Why the Linux error queue matters**: without draining it, no intermediate
hop is ever heard from — every row but the destination reads as a timeout,
and the app looks like a plain ping pretending to be a traceroute. Setup
failure (`IP_RECVERR`) is treated as **fatal**, not ignorable, for exactly
this reason (`tracer.cpp` ~L667).

**Why macOS needs its own echo-id filter** (`filterEchoId = true` on that
platform, `false` on Linux): two concurrent traces (a second OpenMTR, `ping`,
`mtr`) on macOS/BSD would otherwise collide on the same deterministic
sequence number `(hop << 11) | probe_count` and record each other's packets
— a silent hop inherits a fabricated address from a foreign reply, and a
foreign packet clearing `pending` early makes the real reply arrive
"unmatched" (phantom loss). Linux doesn't need this: the kernel already
delivers only matching traffic to each ping socket.

**Sequence number packing** (`tracer.cpp` ~L750): the hop index is packed
into the top 5 bits of `icmp_seq` so a reply can be matched to its hop with
no extra per-hop state on the wire. `static_assert(MAX_HOPS <= 32, ...)`
guards this. The remaining 11 bits are a per-hop probe counter, wrapping at
2048 probes (~34 min at the 1s interval) — chosen so a very late reply can't
match a fresh probe of the same hop (the old 8-bit counter wrapped at ~4 min,
which could).

**Linux `sendto()` interaction with `IP_RECVERR`** (`tracer.cpp` ~L731): a
ping socket with `IP_RECVERR` keeps the *last* queued ICMP error in
`sk_err`, and the *next* `send()` call returns that error instead of
sending. A failed send can therefore be a router's answer to an *earlier*
probe, not a local problem. One immediate retry tells the two apart — the
failed call cleared `sk_err`, so a real local failure fails again, while the
router's entry stays queued for the normal drain path.

**Payload size**: use `ICMP_MINLEN` (8 bytes: type/code/checksum/id/seq),
never `sizeof(struct icmp)` (28 bytes — embeds a whole `struct ip` in a
union) — the latter used to pad every v4 probe with 20 extra bytes, so v4
and v6 measured with different actual wire sizes than the configured
`--size`.

**ICMPv6 error translation** (`Icmp6StatusFor`, `tracer.cpp` ~L64): every
v6 error used to collapse into "Destination host unreachable", hiding two
that actually mean something — Packet Too Big (path MTU smaller than the
probe, nothing to do with reachability) and the unreachable sub-codes
(missing route vs. administrative block). RFC 4443 numbering, mapped onto
the existing `IP_*` status space Windows already defines (see `tracer.h`
~L1556 for the numeric collisions this creates, and why 11004 is the one
exception that doesn't cleanly reuse the IPv4 meaning).

## Thread safety model

- `OpenMTRNet::m_mutex` guards the hop table. All reads go through locked
  accessors; `GetHopSnapshot()` takes a **single lock for every field of one
  hop** so a caller never mixes e.g. `xmit` from one probe cycle with
  `total`/`worst` from the next.
- `GetAddr()` returns **by value**, never a pointer into the table — a
  caller never holds an unsynchronized reference.
- `m_lastAlive`/parking-enabled flag are plain atomics: written under the
  mutex, read lock-free from the dispatch loop once per cycle (a stale read
  here just delays parking by one cycle, not a correctness issue).
- `tracing`/`stopRequested`: `StopTrace()` sets `stopRequested` then
  `tracing = false`; `DoTrace()` sets `tracing = true` then checks for a
  stop — same two steps, opposite order, so whichever side runs first, the
  trace still ends correctly. `stopRequested` is **sticky** (never cleared)
  since a stop landing before `DoTrace()` even starts must still win, and an
  engine only ever runs one trace.
- **Reverse-DNS lifetime** (`DnsSink`, `tracer.h` ~L266): a `getnameinfo()`
  call on a hop with no PTR record can block for seconds, and the user can
  Stop (destroying the engine) meanwhile. A worker holding a raw
  `OpenMTRNet*` would write into freed memory. Fix: a `shared_ptr<DnsSink>`
  jointly owned by the engine and every worker it started; `~OpenMTRNet`
  clears `net` under the mutex, and a worker that finds it null just drops
  its result. This exact bug shipped in 1.3.0 after a fix was lost in an
  upload — see `tests/engine_tests.cpp`'s `dns_lifetime` test, which exists
  specifically to catch a regression of it (aborts/hangs against a
  regressed tree).

## Warm-up / settling (shared with CLI — `report_core.h`)

- **Route fingerprint stability** (`warmupRouteSettled`): hop count + every
  hop's address must hold steady for `kWarmupStableTicks` (5) × 250ms ticks.
  The *whole* fingerprint must hold, not just the count — otherwise middle
  hops still filling in while the destination has already answered would
  pass early.
- **Per-hop guard**: every hop about to be shown must have either answered
  at least once, or sat through **two full probe windows** without
  answering (`xmit >= 2` for a silent hop, since `xmit` only increments
  after a probe completes — two full 5s timeouts is a strong signal it's
  genuinely silent, not about to answer any moment).
- **DNS/ASN settling** (`dnsAndAsnSettled`): every addressed hop has a
  resolved name (or reverse-DNS has stalled a few ticks with nothing new),
  and no ASN lookup is still pending. Never blocks on a result that isn't
  coming.
- An overall deadline (12s route + up to 16s total including DNS/ASN) is
  only a backstop for routes that never stop changing — the fingerprint +
  per-hop guard settle the normal case, including an undiscovered route
  (hop count pinned at `MAX_HOPS`).

## Engine test suite (`tests/engine_tests.cpp`)

No Qt, no window, nothing sent off the test runner. Run via `ctest` in a
separate `-DOPENMTR_BUILD_TESTS=ON` build (see `docs/BUILD_AND_CI.md`).
Notable cases:

- `dns_lifetime` — the use-after-free described above.
- `status_text` — `SetErrorName()` only fills an empty name, *except* a
  "Not sent" text is provisional and gets replaced by the next non-local
  status or a resolved name (so a transient local failure — e.g. IPv6
  dropping for a few seconds — doesn't permanently mislabel an
  otherwise-responding hop).
- `v6_silent_first_hop` — an IPv6 trace whose first hop never answers must
  still find the route's length. `RecalcMaxLocked()` used to infer the
  address family from hop 1's address, so an eternally-silent hop 1 (e.g.
  `fe80::1` with no scope id, which fails every `sendto()` locally) kept
  `GetMax()` pinned at `MAX_HOPS` forever.
- `stop_before_start` / `stop_right_after_start` — a `StopTrace()` landing
  before or immediately after `DoTrace()` starts must still end the trace;
  `DoTrace()` used to unconditionally set `tracing = true` and stomp on an
  already-requested stop.
- Tests that need a real ping socket check `pingSocketAvailable()` first and
  report themselves **skipped** (not passed) if unavailable — see
  `tests/CMakeLists.txt`'s `SKIP_RETURN_CODE 77` — so a sandboxed CI
  runner without ICMP privileges doesn't silently report false confidence.
