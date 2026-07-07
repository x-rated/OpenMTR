// ==========================================================================
//  tracer.cpp — implementation of the ICMP traceroute engine
//
//  A single async dispatch loop drives ICMP probes for every TTL (no thread
//  per hop); per-hop statistics are stored in OpenMTRNet's hop table and
//  read back through locked accessors. A small DNS worker resolves each
//  hop's host name in the background. See the comment above DoTrace() for
//  the dispatch design.
// ==========================================================================

// ==========================================================================
//  Includes & macros
// ==========================================================================

#include "tracer.h"
#include <process.h>
#include <cstring>

// Tell the IP stack not to fragment our probe packets.
#define IPFLAG_DONT_FRAGMENT 0x02

// ==========================================================================
//  Async dispatch state & forward declarations
// ==========================================================================

struct DnsResolverArgs {
    OpenMTRNet* net;
    int        index;
};

// DNS resolution still runs on its own short-lived worker thread (unrelated
// to probe scheduling). Declared here so the Set* mutators can launch it.
static void DnsResolverThread(void* p);


// ==========================================================================
//  Construction & teardown
// ==========================================================================

// Open the capability handles (IPv4 always; IPv6 if the OS provides it) and
// clear the hop table. Failing to get the v4 handle leaves `initialized`
// false; surfacing that to the user is the caller's job — the engine has no
// UI dependency.
OpenMTRNet::OpenMTRNet(const OpenMTROptions& options)
    : opts(options)
{
    memset(m_hops, 0, sizeof(m_hops));
    memset(&last_remote_addr6, 0, sizeof(last_remote_addr6));

    hICMP = IcmpCreateFile();
    if (hICMP == INVALID_HANDLE_VALUE)
        return;

    hICMP6  = Icmp6CreateFile();
    hasIPv6 = (hICMP6 != INVALID_HANDLE_VALUE);

    // Manual-reset: once StopTrace() signals it, it stays signaled for every
    // WaitForMultipleObjects call in the dispatch loop until the next trace
    // resets it, so a stop request can never race a wait that started just
    // before SetEvent() was called.
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent)
        return;

    initialized = true;
}

// Close whatever ICMP handles we opened.
OpenMTRNet::~OpenMTRNet()
{
    if (initialized) {
        if (hasIPv6 && hICMP6 != INVALID_HANDLE_VALUE)
            IcmpCloseHandle(hICMP6);
        if (hICMP != INVALID_HANDLE_VALUE)
            IcmpCloseHandle(hICMP);
    }
    if (m_stopEvent)
        CloseHandle(m_stopEvent);
}

// Zero the entire hop table so a fresh trace starts from clean statistics.
void OpenMTRNet::ResetHops()
{
    memset(m_hops, 0, sizeof(m_hops));
    m_lastAlive.store(0, std::memory_order_relaxed);
    m_parkingEnabled.store(false, std::memory_order_relaxed);
    m_maxHops.store(MAX_HOPS, std::memory_order_relaxed);
}

// Restart statistics from this moment: zero every hop's counters and RTT
// figures while keeping addresses and resolved names, and enable parking of
// probes far beyond the route edge. Called by the UI when the results table
// is revealed, so Loss/Sent/Recv and Best/Avrg/Wrst/Last/Jttr all describe
// the same measurement window instead of mixing in warm-up probes.
void OpenMTRNet::ResetStats()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& h : m_hops) {
        h.xmit      = 0;
        h.returned  = 0;
        h.total     = 0;
        h.jitterSum = 0;
        h.anomalyCount  = 0;
        h.anomalyLast   = 0;
        h.last      = 0;
        h.best      = 0;
        h.worst     = 0;
    }
    m_parkingEnabled.store(true, std::memory_order_relaxed);
}

// ==========================================================================
//  Trace control & async dispatch
// ==========================================================================
//
// DoTrace() drives all MAX_HOPS probes from a single dispatch loop rather
// than one blocking OS thread per TTL. It runs on whichever thread called
// DoTrace() (the wrapper's background thread). Each hop gets its own
// ICMP handle (still one per hop — see the rationale in HopState below) and
// its own Event handle; IcmpSendEcho2()/Icmp6SendEcho2() is submitted with
// that event instead of blocking, and the dispatcher sleeps in
// WaitForMultipleObjects() on every outstanding hop's event plus a stop
// event, waking only when a reply/timeout completes or the next hop's
// scheduled send falls due — never busy-polling, and never holding a
// thread's stack and kernel scheduling slot per TTL.
//
// This relies on documented IP Helper API behaviour: passing a non-NULL
// Event makes the call return immediately (GetLastError() == ERROR_IO_PENDING)
// and signals that event, with the ReplyBuffer already filled in (Status
// included), once a reply arrives or the request times out — the same way the
// Windows SDK's own IcmpSendEcho2 samples use the Event parameter. On a
// timeout the reply buffer's Status reads IP_REQ_TIMED_OUT.

// Per-hop async probe state, owned entirely by the dispatch loop in
// DoTrace() — never touched from another thread, so it needs no locking of
// its own (only the calls into OpenMTRNet's locked accessors/mutators do).
struct HopState {
    int    ttl          = 0;      // 1-based TTL this state probes
    bool   pending       = false; // an IcmpSendEcho2 call is outstanding
    bool   retired       = false; // past the route edge; no longer probed
    HANDLE hIcmp         = INVALID_HANDLE_VALUE;
    HANDLE hEvent        = nullptr;
    IPINFO ipOptions{};
    std::vector<unsigned char> requestData;
    std::vector<unsigned char> replyBuffer;
    // Absolute per-hop schedule: next send is due at t0 + slot*period. t0
    // carries this hop's initial stagger (see the comment in DoTrace());
    // period is intervalMs, shared by every hop. Sends sleep to an absolute
    // target time, evaluated by the dispatcher rather than by each hop on its
    // own stack.
    ULONGLONG t0            = 0;
    ULONGLONG slot          = 0;
    ULONGLONG lastProbeTick = 0;
};

// Start a trace: drive all MAX_HOPS probes from one dispatch loop instead of
// one thread per TTL (see the design note above). Picks the v4 or v6 path
// from the destination's address family.
void OpenMTRNet::DoTrace(sockaddr* dest)
{
    tracing = true;
    ResetHops();
    if (m_stopEvent) ResetEvent(m_stopEvent);

    const bool isV6       = (dest->sa_family == AF_INET6);
    const WORD payloadLen = (WORD)opts.pingsize;

    // Period is the configured interval plus a small fixed offset. ICMP
    // error rate limiters commonly refill on a whole-second cycle; probing
    // at exactly that period would pin every arrival to one fixed phase of
    // the limiter's clock, and a hop parked at the token-refill boundary
    // would drop replies rhythmically. The extra 16 ms sweeps the arrival
    // phase across the limiter's whole cycle once per ~64 probes. Every hop
    // shares the same period, so inter-hop spacing is unaffected.
    ULONGLONG intervalMs = (ULONGLONG)(opts.interval * 1000);
    if (intervalMs == 0) intervalMs = 1000;
    intervalMs += 16;
    const ULONGLONG globalT0 = GetTickCount64();

    IPAddr       destAddr4 = 0;
    sockaddr_in6 destAddr6 = {};
    static const sockaddr_in6 srcAny = { AF_INET6, 0, 0, in6addr_any, 0 };

    if (isV6) {
        m_hops[0].addr6.sin6_family = AF_INET6;
        last_remote_addr6 = ((sockaddr_in6*)dest)->sin6_addr;
        destAddr6 = *(sockaddr_in6*)dest;
    } else {
        m_hops[0].addr.sin_family = AF_INET;
        last_remote_addr = ((sockaddr_in*)dest)->sin_addr;
        destAddr4 = ((sockaddr_in*)dest)->sin_addr.s_addr;
    }

    const size_t replyBufSize =
        (sizeof(ICMP_ECHO_REPLY) > sizeof(ICMPV6_ECHO_REPLY)
            ? sizeof(ICMP_ECHO_REPLY) : sizeof(ICMPV6_ECHO_REPLY)) + 8192;

    std::vector<HopState> hops(MAX_HOPS);
    for (int i = 0; i < MAX_HOPS; ++i) {
        HopState& hs = hops[i];
        hs.ttl = i + 1;

        // Each hop owns a private ICMP handle: a dedicated handle gives the
        // hop's probes a unique ICMP echo identifier on the wire, so stateful
        // devices along the path (which track ICMP flows by that identifier)
        // and Windows itself never have two hops' outstanding requests
        // sharing state.
        hs.hIcmp = isV6 ? Icmp6CreateFile() : IcmpCreateFile();
        if (hs.hIcmp == INVALID_HANDLE_VALUE) { hs.retired = true; continue; }

        hs.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!hs.hEvent) {
            IcmpCloseHandle(hs.hIcmp);
            hs.hIcmp   = INVALID_HANDLE_VALUE;
            hs.retired = true;
            continue;
        }

        hs.requestData.assign(8192, ' ');
        hs.replyBuffer.assign(replyBufSize, 0);
        hs.ipOptions.Ttl   = (UCHAR)hs.ttl;
        hs.ipOptions.Flags = IPFLAG_DONT_FRAGMENT;

        // Initial per-TTL stagger: hop i's schedule starts 50 ms after hop
        // i-1's, so the hops' probes spread across the interval rather than
        // all firing on the same tick.
        hs.t0   = globalT0 + (ULONGLONG)(hs.ttl - 1) * 50;
        hs.slot = 0;
    }

    // Records one completed probe's outcome, whether the completion arrived
    // via the hop's event or (rarely) the call finished inline. Because the
    // call is dispatched asynchronously, the reply buffer's Status field is
    // the only source of truth (there is no live return value to inspect
    // once the event fires) — see the design note above.
    auto complete = [&](HopState& hs) {
        unsigned long status;
        int           rtt;
        if (isV6) {
            auto* r = reinterpret_cast<ICMPV6_ECHO_REPLY*>(hs.replyBuffer.data());
            status = r->Status;
            rtt    = (int)r->RoundTripTime;
            if (status == IP_SUCCESS || status == IP_TTL_EXPIRED_TRANSIT) {
                RecordProbe(hs.ttl - 1, true, rtt);
                SetAddr6(hs.ttl - 1, r->Address);
            }
        } else {
            auto* r = reinterpret_cast<ICMP_ECHO_REPLY*>(hs.replyBuffer.data());
            status = r->Status;
            rtt    = (int)r->RoundTripTime;
            if (status == IP_SUCCESS || status == IP_TTL_EXPIRED_TRANSIT) {
                RecordProbe(hs.ttl - 1, true, rtt);
                SetAddr(hs.ttl - 1, r->Address);
            }
        }
        if (status != IP_SUCCESS && status != IP_TTL_EXPIRED_TRANSIT) {
            SetErrorName(hs.ttl - 1, status);
            // A plain timeout carries no anomaly (RecordProbe's contract);
            // any other status is a genuine anomaly to surface.
            RecordProbe(hs.ttl - 1, false, 0, status == IP_REQ_TIMED_OUT ? 0ul : status);
        }
        hs.pending = false;
        // Advance to the next slot strictly in the future: a reply lands
        // within the hop's slot (next = slot + 1); a full timeout skips the
        // slots it consumed and resumes on schedule, so the hop's phase
        // survives timeouts/parking unchanged.
        hs.slot = (GetTickCount64() - hs.t0) / intervalMs + 1;
    };

    // Submits hop `hs`'s next probe. Async dispatch (the common case) just
    // marks it pending and returns; the dispatch loop below waits for its
    // event. A hard failure at submission time (not a network timeout — the
    // call itself couldn't be started) is recorded as an anomaly right away.
    auto submit = [&](HopState& hs) {
        DWORD ret = isV6
            ? Icmp6SendEcho2(hs.hIcmp, hs.hEvent, nullptr, nullptr,
                              const_cast<sockaddr_in6*>(&srcAny), &destAddr6,
                              hs.requestData.data(), payloadLen,
                              &hs.ipOptions, hs.replyBuffer.data(), (DWORD)hs.replyBuffer.size(),
                              ECHO_REPLY_TIMEOUT)
            : IcmpSendEcho2(hs.hIcmp, hs.hEvent, nullptr, nullptr,
                             destAddr4, hs.requestData.data(), payloadLen,
                             &hs.ipOptions, hs.replyBuffer.data(), (DWORD)hs.replyBuffer.size(),
                             ECHO_REPLY_TIMEOUT);
        hs.lastProbeTick = GetTickCount64();

        if (ret != 0) {
            complete(hs); // finished inline (e.g. loopback) — no event wait
            return;
        }
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            hs.pending = true;
        } else {
            SetErrorName(hs.ttl - 1, err);
            RecordProbe(hs.ttl - 1, false, 0, err);
            hs.slot = (GetTickCount64() - hs.t0) / intervalMs + 1;
        }
    };

    auto retire = [&](HopState& hs) {
        if (hs.hEvent) { CloseHandle(hs.hEvent); hs.hEvent = nullptr; }
        if (hs.hIcmp != INVALID_HANDLE_VALUE) { IcmpCloseHandle(hs.hIcmp); hs.hIcmp = INVALID_HANDLE_VALUE; }
        hs.retired = true;
    };

    // Next scheduled send time for a hop that isn't pending/retired: its
    // normal slot, or — once parking kicks in for a hop stuck well past the
    // route's known edge (UNKNOWN_HOP_MARGIN) — the end of its patrol window
    // (UNKNOWN_PATROL_MS), so probes that would only die unanswered stop
    // spamming rate-limited routers and get sent just occasionally instead.
    auto nextDue = [&](const HopState& hs) -> ULONGLONG {
        if (IsParkingEnabled() && hs.lastProbeTick != 0 &&
            hs.ttl > GetLastAlive() + UNKNOWN_HOP_MARGIN) {
            return hs.lastProbeTick + UNKNOWN_PATROL_MS;
        }
        return hs.t0 + hs.slot * intervalMs;
    };

    bool anyActive = true;
    while (tracing && anyActive) {
        anyActive = false;
        const ULONGLONG now = GetTickCount64();

        // 1) Submit every hop whose send slot has arrived.
        for (auto& hs : hops) {
            if (hs.retired) continue;
            if (hs.pending) { anyActive = true; continue; }

            if (hs.ttl > GetMax()) { retire(hs); continue; }
            anyActive = true;

            if (now >= nextDue(hs)) submit(hs);
        }

        if (!anyActive || !tracing) break;

        // 2) Wait for whichever comes first: a hop's reply/timeout event,
        //    the stop event, or the next hop's scheduled send time.
        HANDLE handles[MAX_HOPS + 1];
        int    handleHop[MAX_HOPS + 1];
        int    count = 0;
        handles[count] = m_stopEvent; handleHop[count] = -1; ++count;
        for (auto& hs : hops) {
            if (hs.pending) { handles[count] = hs.hEvent; handleHop[count] = hs.ttl - 1; ++count; }
        }

        ULONGLONG soonest = GetTickCount64() + 250;
        for (auto& hs : hops) {
            if (hs.retired || hs.pending) continue;
            ULONGLONG due = nextDue(hs);
            if (due < soonest) soonest = due;
        }
        const ULONGLONG nowWait = GetTickCount64();
        const ULONGLONG rest    = (soonest > nowWait) ? (soonest - nowWait) : 0;
        const DWORD     timeoutMs = (DWORD)(rest < 250 ? rest : 250);

        DWORD w = WaitForMultipleObjects(count, handles, FALSE, timeoutMs);
        if (!tracing) break;
        if (w == WAIT_TIMEOUT || w == WAIT_FAILED) continue;

        int idx = (int)(w - WAIT_OBJECT_0);
        if (idx <= 0 || idx >= count) continue; // stop event, or spurious
        int hopIndex = handleHop[idx];
        if (hopIndex >= 0) complete(hops[hopIndex]);
    }

    for (auto& hs : hops) retire(hs);
    tracing = false;
}

// Signal the dispatch loop to stop: flip the flag it polls every pass, and
// wake it immediately from WaitForMultipleObjects() rather than making it
// wait out its current timeout slice.
void OpenMTRNet::StopTrace()
{
    tracing = false;
    if (m_stopEvent) SetEvent(m_stopEvent);
}

// Resolve one hop's address to a host name: numeric form first, then a
// reverse-DNS name if DNS lookups are enabled.
static void DnsResolverThread(void* p)
{
    auto* args     = (DnsResolverArgs*)p;
    OpenMTRNet* net = args->net;
    char hostname[NI_MAXHOST];

    // Thread-safe copy of the hop's address (see OpenMTRNet::GetAddr). The
    // sockaddr length passed to getnameinfo must match the address family —
    // passing the larger sockaddr_in6 size for a v4 hop would be a
    // family/length mismatch that can make the lookup fail silently.
    SOCKADDR_INET addr  = net->GetAddr(args->index);
    const bool    isV6  = (addr.si_family == AF_INET6);
    sockaddr*     sa    = isV6 ? (sockaddr*)&addr.Ipv6 : (sockaddr*)&addr.Ipv4;
    socklen_t     salen = isV6 ? sizeof(sockaddr_in6)  : sizeof(sockaddr_in);

    if (!getnameinfo(sa, salen, hostname, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST)) {
        net->SetName(args->index, hostname);
    }

    if (net->opts.useDNS) {
        if (!getnameinfo(sa, salen, hostname, NI_MAXHOST, nullptr, 0, 0)) {
            net->SetName(args->index, hostname);
        }
    }

    delete args;
}

// ==========================================================================
//  Read accessors (locked)
// ==========================================================================

// Thread-safe copy of hop `at`'s address (v4 or v6). Returned by value, not
// as a pointer into the hop table, so the caller never holds a reference
// that could be read while another thread is writing it under m_mutex.
SOCKADDR_INET OpenMTRNet::GetAddr(int at)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SOCKADDR_INET r = {};
    if (m_hops[at].addr6.sin6_family == AF_INET6) {
        r.si_family = AF_INET6;
        r.Ipv6      = m_hops[at].addr6;
    } else if (m_hops[at].addr.sin_family == AF_INET) {
        r.si_family = AF_INET;
        r.Ipv4      = m_hops[at].addr;
    } else {
        r.si_family = AF_UNSPEC;
    }
    return r;
}

// Full snapshot of hop `at` under a single lock — see the declaration in
// tracer.h for why this replaces taking the per-field getters one at a time.
OpenMTRNet::HopSnapshot OpenMTRNet::GetHopSnapshot(int at)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const HopRecord& h = m_hops[at];
    HopSnapshot s;

    if (h.addr6.sin6_family == AF_INET6) {
        s.addr.si_family = AF_INET6;
        s.addr.Ipv6      = h.addr6;
    } else if (h.addr.sin_family == AF_INET) {
        s.addr.si_family = AF_INET;
        s.addr.Ipv4      = h.addr;
    } else {
        s.addr.si_family = AF_UNSPEC;
    }

    s.xmit         = h.xmit;
    s.returned     = h.returned;
    s.altCount     = h.altCount;
    if (s.altCount > 0) s.altAddr = h.altAddr;
    s.anomalyCount = h.anomalyCount;
    s.anomalyLast  = h.anomalyLast;
    s.best         = h.best;
    s.worst        = h.worst;
    s.last         = h.last;
    s.total        = h.total;
    s.jitterSum    = h.jitterSum;
    strcpy_s(s.name, sizeof(s.name), h.name);
    return s;
}

// Route length as currently displayed: index just past the hop that carries
// the target address, or MAX_HOPS with trailing duplicates of the last
// responder trimmed. Reads the cache maintained by the address mutators
// (see RecalcMaxLocked), so it is a lock-free load, not a table scan.
int OpenMTRNet::GetMax()
{
    return m_maxHops.load(std::memory_order_relaxed);
}

// Recompute the route length; the caller must hold m_mutex. Scans the hop
// table until the target address appears (that index is the hop count). If
// the target was never seen (result == MAX_HOPS), trims trailing hops that
// merely repeat the last address or are empty, so the table doesn't end in
// identical/blank rows. Handles IPv4 and IPv6 separately.
int OpenMTRNet::RecalcMaxLocked()
{
    int max = 0;
    if (m_hops[0].addr6.sin6_family == AF_INET6) {
        for (; max < MAX_HOPS && memcmp(&m_hops[max++].addr6.sin6_addr, &last_remote_addr6, sizeof(in6_addr)););
        if (max == MAX_HOPS) {
            while (max > 1
                && !memcmp(&m_hops[max-1].addr6.sin6_addr, &m_hops[max-2].addr6.sin6_addr, sizeof(in6_addr))
                && (m_hops[max-1].addr6.sin6_addr.u.Word[0] | m_hops[max-1].addr6.sin6_addr.u.Word[1]
                  | m_hops[max-1].addr6.sin6_addr.u.Word[2] | m_hops[max-1].addr6.sin6_addr.u.Word[3]
                  | m_hops[max-1].addr6.sin6_addr.u.Word[4] | m_hops[max-1].addr6.sin6_addr.u.Word[5]
                  | m_hops[max-1].addr6.sin6_addr.u.Word[6] | m_hops[max-1].addr6.sin6_addr.u.Word[7]))
                --max;
        }
    } else {
        for (; max < MAX_HOPS && m_hops[max++].addr.sin_addr.s_addr != last_remote_addr.s_addr;);
        if (max == MAX_HOPS) {
            while (max > 1
                && m_hops[max-1].addr.sin_addr.s_addr == m_hops[max-2].addr.sin_addr.s_addr
                && m_hops[max-1].addr.sin_addr.s_addr)
                --max;
        }
    }
    return max;
}

// ==========================================================================
//  Mutators (locked)
// ==========================================================================

// Record hop `at`'s IPv4 address the first time we see it, and launch a
// background DNS resolver for it.
void OpenMTRNet::SetAddr(int at, u_long addr)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hops[at].addr.sin_addr.s_addr != 0 &&
        m_hops[at].addr.sin_addr.s_addr != addr) {
        // A reply from a different address than the recorded one: a route
        // change or per-packet load balancing. The primary address stays
        // stable; remember the latest differing responder and count it.
        m_hops[at].altAddr = {};
        m_hops[at].altAddr.Ipv4.sin_family      = AF_INET;
        m_hops[at].altAddr.Ipv4.sin_addr.s_addr = addr;
        ++m_hops[at].altCount;
    }
    if (m_hops[at].addr.sin_addr.s_addr == 0) {
        m_hops[at].addr.sin_family      = AF_INET;
        m_hops[at].addr.sin_addr.s_addr = addr;
        auto* args  = new DnsResolverArgs;
        args->index = at;
        args->net   = this;
        if (_beginthread(DnsResolverThread, 0, args) == static_cast<uintptr_t>(-1))
            delete args;
    }
    m_maxHops.store(RecalcMaxLocked(), std::memory_order_relaxed);
}

// IPv6 version of SetAddr(): store the address once, then resolve its name.
void OpenMTRNet::SetAddr6(int at, IPV6_ADDRESS_EX addrex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& w = m_hops[at].addr6.sin6_addr.u.Word;
    bool empty = !(w[0]|w[1]|w[2]|w[3]|w[4]|w[5]|w[6]|w[7]);
    if (!empty) {
        bool differs = false;
        for (int i = 0; i < 8 && !differs; ++i)
            differs = (w[i] != addrex.sin6_addr[i]);
        if (differs) {
            // See the IPv4 counterpart above.
            m_hops[at].altAddr = {};
            m_hops[at].altAddr.Ipv6.sin6_family = AF_INET6;
            for (int i = 0; i < 8; ++i)
                m_hops[at].altAddr.Ipv6.sin6_addr.u.Word[i] = addrex.sin6_addr[i];
            ++m_hops[at].altCount;
        }
    }
    if (empty) {
        m_hops[at].addr6.sin6_family = AF_INET6;
        for (int i = 0; i < 8; ++i)
            m_hops[at].addr6.sin6_addr.u.Word[i] = addrex.sin6_addr[i];
        auto* args  = new DnsResolverArgs;
        args->index = at;
        args->net   = this;
        if (_beginthread(DnsResolverThread, 0, args) == static_cast<uintptr_t>(-1))
            delete args;
    }
    m_maxHops.store(RecalcMaxLocked(), std::memory_order_relaxed);
}

// Store the resolved host name for hop `at`.
void OpenMTRNet::SetName(int at, char* n)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    strcpy_s(m_hops[at].name, sizeof(m_hops[at].name), n);
}

// Translate a Win32 ICMP status code into readable text. Only applied when
// the hop has no name yet, so a real host name always wins.
void OpenMTRNet::SetErrorName(int at, DWORD errnum)
{
    const char* name;
    switch (errnum) {
    case IP_BUF_TOO_SMALL:            name = "Reply buffer too small."; break;
    case IP_DEST_NET_UNREACHABLE:     name = "Destination network unreachable."; break;
    case IP_DEST_HOST_UNREACHABLE:    name = "Destination host unreachable."; break;
    case IP_DEST_PROT_UNREACHABLE:    name = "Destination protocol unreachable."; break;
    case IP_DEST_PORT_UNREACHABLE:    name = "Destination port unreachable."; break;
    case IP_NO_RESOURCES:             name = "Insufficient IP resources."; break;
    case IP_BAD_OPTION:               name = "Bad IP option."; break;
    case IP_HW_ERROR:                 name = "Hardware error."; break;
    case IP_PACKET_TOO_BIG:           name = "Packet too big."; break;
    case IP_REQ_TIMED_OUT:            name = "Request timed out."; break;
    case IP_BAD_REQ:                  name = "Bad request."; break;
    case IP_BAD_ROUTE:                name = "Bad route."; break;
    case IP_TTL_EXPIRED_REASSEM:      name = "TTL expired during reassembly."; break;
    case IP_PARAM_PROBLEM:            name = "Parameter problem."; break;
    case IP_SOURCE_QUENCH:            name = "Source quench."; break;
    case IP_OPTION_TOO_BIG:           name = "IP option too big."; break;
    case IP_BAD_DESTINATION:          name = "Bad destination."; break;
    case IP_GENERAL_FAILURE:          name = "General failure."; break;
    default:                          name = "Unknown error."; break;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!*m_hops[at].name)
        strcpy_s(m_hops[at].name, sizeof(m_hops[at].name), name);
}

// Record one completed probe atomically: bump xmit and, per outcome, either
// the reply statistics (returned, RTT figures, jitter) or the anomaly
// bookkeeping. A single lock covers everything, so a snapshot can never see
// a probe as sent-but-unaccounted. Plain timeouts pass anomaly == 0.
void OpenMTRNet::RecordProbe(int at, bool replied, int rtt, unsigned long anomaly)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_hops[at].xmit;
    if (replied) {
        // Jitter: |difference| against the previous reply's RTT; the very
        // first reply has no predecessor and contributes no difference.
        if (m_hops[at].returned > 0)
            m_hops[at].jitterSum += (rtt > m_hops[at].last) ? (unsigned long)(rtt - m_hops[at].last)
                                                            : (unsigned long)(m_hops[at].last - rtt);
        m_hops[at].last   = rtt;
        m_hops[at].total += rtt;
        if (m_hops[at].returned == 0 || m_hops[at].best > rtt) m_hops[at].best = rtt;
        if (m_hops[at].worst < rtt)                            m_hops[at].worst = rtt;
        ++m_hops[at].returned;
        // Track the farthest hop that ever answered (1-based); written under
        // m_mutex, read lock-free by the dispatch loop's parking logic.
        if (at + 1 > m_lastAlive.load(std::memory_order_relaxed))
            m_lastAlive.store(at + 1, std::memory_order_relaxed);
    } else if (anomaly != 0) {
        ++m_hops[at].anomalyCount;
        m_hops[at].anomalyLast = anomaly;
    }
}

