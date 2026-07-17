// ==========================================================================
//  tracer.h — ICMP traceroute engine (network back-end)
//
//  Two layers:
//    * OpenMTRNet         — low-level engine. A single dispatch loop (run on
//                           the caller's thread — see OpenMTRNetWrapper)
//                           drives async ICMP probes for every TTL; results
//                           are stored in a fixed hop table guarded by a
//                           mutex. See the comment above DoTrace() in
//                           tracer.cpp for the dispatch design.
//    * OpenMTRNetWrapper  — thread-friendly facade used by the UI. Owns an
//                           OpenMTRNet on a background thread and hands out
//                           immutable per-hop snapshots.
//
//  Derived from WinMTR Redux / WinMTR (GPL v2).
// ==========================================================================

#pragma once

// ==========================================================================
//  Includes
// ==========================================================================

#ifdef _WIN32
// Windows networking / ICMP — the two defines must precede <winsock2.h>.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

// Windows API types used throughout this file, backed by their POSIX
// equivalents so the ICMP engine below doesn't need a #ifdef at every use.
typedef void*          HWND;
typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef unsigned char  BYTE;
typedef unsigned short WORD;
#ifdef __APPLE__
typedef bool BOOL;
#else
typedef int BOOL;
#endif
#define FALSE 0
#define TRUE  1
typedef unsigned long long ULONGLONG;
typedef void* HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)

typedef struct
{
    BYTE  Ttl;
    BYTE  Tos;
    BYTE  Flags;
    BYTE  OptionsSize;
    unsigned char* OptionsData;
} IP_OPTION_INFORMATION;

typedef struct
{
    in_addr        Address;
    unsigned long  Status;
    unsigned long  RoundTripTime;
    unsigned short DataSize;
    unsigned short Reserved;
    void*          Data;
    IP_OPTION_INFORMATION Options;
} ICMP_ECHO_REPLY;

typedef struct
{
    in6_addr      Address;
    unsigned long Status;
    unsigned long RoundTripTime;
} ICMPV6_ECHO_REPLY;

union SOCKADDR_INET
{
    sockaddr_in  Ipv4;
    sockaddr_in6 Ipv6;
    unsigned short si_family;
};

typedef struct
{
    unsigned short sin6_port;
    unsigned long  sin6_flowinfo;
    unsigned short sin6_addr[8];
    unsigned long  sin6_scope_id;
} IPV6_ADDRESS_EX;

// IP_STATUS values normally returned by IcmpSendEcho2 (icmpapi.h), needed by
// the POSIX-side dispatch loop to report the same failure codes as Windows.
#define IP_BUF_TOO_SMALL          11001
#define IP_DEST_NET_UNREACHABLE   11002
#define IP_DEST_HOST_UNREACHABLE  11003
#define IP_DEST_PROT_UNREACHABLE  11004
#define IP_DEST_PORT_UNREACHABLE  11005
#define IP_NO_RESOURCES           11006
#define IP_BAD_OPTION             11007
#define IP_HW_ERROR               11008
#define IP_PACKET_TOO_BIG         11009
#define IP_REQ_TIMED_OUT          11010
#define IP_BAD_REQ                11011
#define IP_BAD_ROUTE              11012
#define IP_TTL_EXPIRED_REASSEM    11013
#define IP_PARAM_PROBLEM          11014
#define IP_SOURCE_QUENCH          11015
#define IP_OPTION_TOO_BIG         11016
#define IP_BAD_DESTINATION        11017
#define IP_GENERAL_FAILURE        11050

// System-menu command IDs (winuser.h) — used by MainWindow's custom title
// bar (captionLabelFromSystem(), GetSystemMenu()) which includes this header
// before its own Windows includes.
#define SC_MINIMIZE 0xF020
#define SC_CLOSE    0xF060
#endif

// C++ standard library.
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <stop_token>
#include <memory>
#include <chrono>

// ==========================================================================
//  Constants
// ==========================================================================

// Highest TTL we probe (i.e. maximum number of hops shown).
#define MAX_HOPS        30
// How long IcmpSendEcho2 waits for a reply, in ms. Besides catching slow
// replies, this also throttles probing of silent hops
// (a timed-out probe blocks for the full window and is not followed by an
// interval sleep), keeping the sustained probe rate low enough not to trip
// ICMPv6 error rate limiters along the path (RFC 4443 token buckets).
#define ECHO_REPLY_TIMEOUT 5000

// Probes this many TTLs beyond the last hop that ever answered still run at
// full rate (so hops just past the known route edge are picked up quickly);
// anything farther is parked once loss counting starts and only sends a
// patrol probe every UNKNOWN_PATROL_MS. Such probes cross the whole path
// only to die unanswered — they carry no information but feed ICMP rate
// limiters on every router along the way, degrading the useful measurements.
#define UNKNOWN_HOP_MARGIN 3
#define UNKNOWN_PATROL_MS  20000

// Shorthand for the Win32 IP option block passed to IcmpSendEcho2.
typedef IP_OPTION_INFORMATION IPINFO;

// ==========================================================================
//  Configuration & per-hop record
// ==========================================================================

// Knobs handed to the engine when a trace starts.
struct OpenMTROptions {
    unsigned pingsize = 64;
    double   interval = 1.0;
    bool     useDNS   = true;
};

// Raw per-hop record kept inside OpenMTRNet: the hop's address (v4 and v6
// share the union), running RTT statistics, anomaly bookkeeping and the
// resolved host name.
struct HopRecord {
    union {
        sockaddr_in  addr;
        sockaddr_in6 addr6;
    };
    int           xmit;
    int           returned;
    SOCKADDR_INET altAddr;     // most recent responder differing from `addr`
    int           altCount;    // replies that arrived from a differing address
    int           anomalyCount;    // completions that were neither a clean reply
                               // nor a plain timeout (non-success ICMP status
                               // in a reply, or a soft failure of the call)
    unsigned long anomalyLast;     // most recent such status / error code
    unsigned long total;
    unsigned long jitterSum;   // sum of |RTT - previous RTT| over consecutive replies
    int           last;
    int           best;
    int           worst;
    char          name[256];
};

// ==========================================================================
//  OpenMTRNet — low-level ICMP engine
// ==========================================================================

// Low-level traceroute engine. DoTrace() runs a single async dispatch loop
// that repeatedly pings every hop and feeds results back through the
// thread-safe accessors/mutators below (all serialized by m_mutex).
class OpenMTRNet
{
public:
    // Lifecycle and trace control.
    explicit OpenMTRNet(const OpenMTROptions& opts);
    ~OpenMTRNet();

    void DoTrace(sockaddr* dest);
    void StopTrace();
    void ResetHops();
    // Zero every hop's counters and RTT statistics (addresses and names are
    // kept) and enable parking of probes far beyond the route edge. Called by
    // the UI when the table is revealed, so that displayed statistics all
    // start from the same moment instead of mixing in warm-up probes.
    void ResetStats();

    // Full thread-safe snapshot of hop `at`, taken under a single lock so a
    // caller never sees a torn mix of counters from different probe cycles
    // (e.g. `xmit` from one update racing with `total` from the next).
    struct HopSnapshot {
        SOCKADDR_INET addr = {};
        int           xmit = 0;
        int           returned = 0;
        SOCKADDR_INET altAddr = {};
        int           altCount = 0;
        int           anomalyCount = 0;
        unsigned long anomalyLast = 0;
        int           best = 0;
        int           worst = 0;
        int           last = 0;
        unsigned long total = 0;
        unsigned long jitterSum = 0;
        char          name[256] = {};
    };
    HopSnapshot GetHopSnapshot(int at);

    // Thread-safe read accessors.
    // GetAddr() returns a copy (not a pointer into the locked hop table) so
    // the caller never holds an unsynchronized reference to internal state.
    // Per-field reads of the rest of a hop's stats go through
    // GetHopSnapshot() above instead of one accessor per field, so a caller
    // never assembles a row from values taken at different instants.
    SOCKADDR_INET GetAddr(int at);
    int  GetMax();

    // Lock-free reads used by the dispatch loop's parking logic: index
    // (1-based hop number) of the farthest hop that ever answered, and
    // whether parking is active (it is enabled together with ResetStats,
    // i.e. only once the warm-up/discovery phase is over and every TTL has
    // had its fair chance).
    int  GetLastAlive() const   { return m_lastAlive.load(std::memory_order_relaxed); }
    bool IsParkingEnabled() const { return m_parkingEnabled.load(std::memory_order_relaxed); }

    // Thread-safe mutators. Called from the dispatch loop for every probe
    // outcome, and from the background DNS resolver thread for SetName().
    void SetAddr(int at, u_long addr);
    void SetAddr6(int at, IPV6_ADDRESS_EX addrex);
    void SetName(int at, char* n);
    void SetErrorName(int at, DWORD errnum);
    // One completed probe: bumps xmit and, per outcome, either the reply
    // statistics (returned, RTT figures, jitter) or the anomaly bookkeeping.
    // Everything is updated under a single lock, so a snapshot can never see
    // a probe as sent-but-unaccounted. Plain timeouts pass anomaly == 0.
    void RecordProbe(int at, bool replied, int rtt, unsigned long anomaly = 0);

    // Shared state read across threads: live flags, last target, handles.
    // Cross-thread flags: `tracing` is written by Stop/DoTrace and polled
    // once per dispatch-loop pass; the other two are written once during
    // construction.
    std::atomic<bool> tracing{false};
    std::atomic<bool> hasIPv6{false};
    std::atomic<bool> initialized{false};

    union {
        in_addr  last_remote_addr;
        in6_addr last_remote_addr6;
    };

    OpenMTROptions opts;

    // Capability probes only (v4 availability check in the constructor and
    // IPv6 support detection). The probing itself uses a private handle per
    // hop — see the HopState comment above DoTrace() in tracer.cpp.
    HANDLE hICMP  = INVALID_HANDLE_VALUE;
    HANDLE hICMP6 = INVALID_HANDLE_VALUE;

private:
    HopRecord    m_hops[MAX_HOPS];
    std::mutex   m_mutex;
    // Farthest 1-based hop number that ever answered (0 = none yet) and the
    // parking switch. Atomics: written under m_mutex, read lock-free from the
    // dispatch loop once per probe cycle.
    std::atomic<int>  m_lastAlive{0};
    std::atomic<bool> m_parkingEnabled{false};
    // Cached route length, recomputed under m_mutex whenever a hop address
    // changes and read lock-free by the dispatch loop once per probe cycle.
    std::atomic<int>  m_maxHops{MAX_HOPS};

    // Wakes the async dispatch loop in DoTrace() immediately when
    // StopTrace() is called, instead of leaving it to notice on its next
    // wait timeout (WaitForMultipleObjects on Windows, poll() on POSIX via
    // a self-pipe). Created in the constructor, closed in the destructor;
    // owned exclusively by this object.
#ifdef _WIN32
    HANDLE m_stopEvent = nullptr;
#else
    int m_stopPipe[2] = {-1, -1};
#endif

    int RecalcMaxLocked();
};

// ==========================================================================
//  UI bridge & per-hop snapshot
// ==========================================================================

// Lets the engine read the current ping size from the UI without depending
// on any concrete widget type.
struct IOpenMTROptionsProvider {
    virtual ~IOpenMTROptionsProvider() = default;
    [[nodiscard]] virtual unsigned getPingSize() const noexcept = 0;
};

// Immutable snapshot of one hop handed to the UI thread. getAvg() derives the
// average RTT from the running total so callers never divide by zero.
struct OpenMTRHostInfo {
    SOCKADDR_INET addr = {};
    int           xmit     = 0;
    int           returned = 0;
    SOCKADDR_INET altAddr  = {};
    int           altCount = 0;
    int           anomalyCount = 0;
    unsigned long anomalyLast  = 0;
    int           best     = 0;
    int           worst    = 0;
    int           last     = 0;
    unsigned long total    = 0;
    unsigned long jitterSum = 0;
    std::wstring  m_name;

    std::wstring getName() const { return m_name; }
    int getAvg() const {
        return (returned == 0) ? 0 : static_cast<int>(total / returned);
    }
    // Mean absolute difference between consecutive RTTs (mtr-style jitter).
    // Needs at least two replies to have one difference.
    int getJitter() const {
        return (returned < 2) ? 0 : static_cast<int>(jitterSum / (returned - 1));
    }
};

// Format a SOCKADDR_INET (v4 or v6) as a printable address string.
inline std::wstring addr_to_wstring(const SOCKADDR_INET& addr)
{
#ifdef _WIN32
    wchar_t buf[64] = {};
    if      (addr.si_family == AF_INET)
        InetNtopW(AF_INET,  &addr.Ipv4.sin_addr,  buf, 64);
    else if (addr.si_family == AF_INET6)
        InetNtopW(AF_INET6, &addr.Ipv6.sin6_addr, buf, 64);
    return buf;
#else
    // No wide-string ntop on POSIX: convert the narrow result instead. Every
    // byte is ASCII (digits, dots, colons, hex), so the widen is lossless.
    char buf[64] = {};
    if      (addr.Ipv4.sin_family  == AF_INET)
        inet_ntop(AF_INET,  &addr.Ipv4.sin_addr,  buf, sizeof(buf));
    else if (addr.Ipv6.sin6_family == AF_INET6)
        inet_ntop(AF_INET6, &addr.Ipv6.sin6_addr, buf, sizeof(buf));
    std::string s(buf);
    return std::wstring(s.begin(), s.end());
#endif
}

// ==========================================================================
//  OpenMTRNetWrapper — threaded facade
// ==========================================================================

// Owns an OpenMTRNet on a detached worker thread and exposes a small, safe
// surface to the UI: DoTrace() to start, getCurrentState() for a snapshot of
// every hop, and isDone()/GetMax() for progress. The destructor stops the
// trace and joins the thread so nothing outlives the wrapper.
class OpenMTRNetWrapper
{
public:
    explicit OpenMTRNetWrapper(IOpenMTROptionsProvider* provider)
        : m_provider(provider)
    {}

    ~OpenMTRNetWrapper()
    {
        if (m_thread.joinable()) {
            if (m_net) m_net->StopTrace();
            m_thread.join();
        }
    }

    int DoTrace(std::stop_token stopToken, SOCKADDR_INET dest)
    {
        m_done.store(false);

        OpenMTROptions opts;
        opts.pingsize = m_provider->getPingSize();
        opts.interval = 1.0;
        opts.useDNS   = true;

        m_net = std::make_unique<OpenMTRNet>(opts);
        // An engine-level failure (the ICMP capability handle could not be
        // opened) is surfaced here, still on the caller's (UI) thread; the
        // engine itself carries no UI dependency.
        if (!m_net->initialized) {
#ifdef _WIN32
            MessageBoxW(nullptr, L"Error opening ICMP handle!", L"OpenMTR",
                        MB_OK | MB_ICONERROR);
#else
            fprintf(stderr, "Error opening ICMP handle!\n");
#endif
            m_done.store(true);
            return -1;
        }

        sockaddr_storage ss = {};
        if (dest.Ipv4.sin_family == AF_INET) {
            auto* s4 = (sockaddr_in*)&ss;
#ifdef __APPLE__
            s4->sin_len = sizeof(sockaddr_in);
#endif
            s4->sin_family = AF_INET;
            s4->sin_addr   = dest.Ipv4.sin_addr;
        } else {
            auto* s6 = (sockaddr_in6*)&ss;
#ifdef __APPLE__
            s6->sin6_len = sizeof(sockaddr_in6);
#endif
            s6->sin6_family = AF_INET6;
            s6->sin6_addr   = dest.Ipv6.sin6_addr;
        }

        // Run the trace on a worker thread. A stop_callback (no extra thread
        // needed) turns a stop request into StopTrace(); the dispatch loop
        // inside DoTrace() already wakes on its own internal event, so there
        // is nothing left here to bridge with a condition variable — the
        // callback just needs to exist for the trace's duration, which is
        // exactly its scope in this lambda.
        m_thread = std::thread([this, ss, stopToken]() mutable {
            std::stop_callback stopper(stopToken, [this] {
                if (m_net) m_net->StopTrace();
            });
            m_net->DoTrace((sockaddr*)&ss);
            m_done.store(true);
        });

        return 0;
    }

    // Build a snapshot of every hop for the UI thread to render.
    std::vector<OpenMTRHostInfo> getCurrentState() const
    {
        std::vector<OpenMTRHostInfo> state;
        if (!m_net) return state;

        int maxHops = m_net->GetMax();
        int rows = (maxHops > 0 && maxHops <= MAX_HOPS) ? maxHops : MAX_HOPS;

        for (int i = 0; i < rows; ++i) {
            // One lock per hop instead of a separate locked call per field:
            // every field below comes from the same instant, so a row can
            // never mix e.g. `xmit` from one probe cycle with `total`/`worst`
            // from the next.
            auto snap = m_net->GetHopSnapshot(i);

            OpenMTRHostInfo h;
            h.addr         = snap.addr;
            h.xmit         = snap.xmit;
            h.returned     = snap.returned;
            h.best         = snap.best;
            h.worst        = snap.worst;
            h.last         = snap.last;
            h.total        = snap.total;
            h.jitterSum    = snap.jitterSum;
            h.altCount     = snap.altCount;
            if (h.altCount > 0) h.altAddr = snap.altAddr;
            h.anomalyCount = snap.anomalyCount;
            h.anomalyLast  = snap.anomalyLast;

            if (snap.name[0])
                h.m_name = std::wstring(snap.name, snap.name + strlen(snap.name));
            else if (h.addr.Ipv4.sin_family != AF_UNSPEC)
                h.m_name = addr_to_wstring(h.addr);

            state.push_back(h);
        }
        return state;
    }

    bool isDone()  const { return m_done.load(); }
    int  GetMax()  const { return m_net ? m_net->GetMax() : MAX_HOPS; }
    // Restart statistics from this moment (see OpenMTRNet::ResetStats).
    void resetStats() { if (m_net) m_net->ResetStats(); }

private:
    IOpenMTROptionsProvider*    m_provider;
    std::unique_ptr<OpenMTRNet> m_net;
    std::thread                m_thread;
    std::atomic<bool>          m_done{ true };
};
