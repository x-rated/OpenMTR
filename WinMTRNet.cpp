//*****************************************************************************
// WinMTRNet.cpp — White-Tiger WinMTR Redux engine, de-MFC'd for Qt6
//*****************************************************************************

#include "WinMTRNet.h"
#include <process.h>
#include <string.h>
#include <cstring>

#ifdef _DEBUG
#  define TRACE_MSG(msg) OutputDebugStringA((std::string() + msg + "\n").c_str())
#else
#  define TRACE_MSG(msg)
#endif

#define IPFLAG_DONT_FRAGMENT 0x02

// ── Thread param structs ──────────────────────────────────────────────────────

struct trace_thread {
    WinMTRNet* winmtr;
    in_addr    address;
    int        ttl;
};

struct trace_thread6 {
    WinMTRNet*   winmtr;
    sockaddr_in6 address;
    int          ttl;
};

struct dns_resolver_thread {
    WinMTRNet* winmtr;
    int        index;
};

// ── Forward declarations ──────────────────────────────────────────────────────

static unsigned WINAPI TraceThread(void* p);
static unsigned WINAPI TraceThread6(void* p);
static void            DnsResolverThread(void* p);

// ── Constructor / Destructor ──────────────────────────────────────────────────

WinMTRNet::WinMTRNet(const WinMTROptions& options)
    : opts(options)
{
    memset(host, 0, sizeof(host));
    memset(&last_remote_addr6, 0, sizeof(last_remote_addr6));

    ghMutex = CreateMutex(nullptr, FALSE, nullptr);
    if (!ghMutex) return;

    // WSAStartup is called once globally in main.cpp — no need to repeat it here.

    hICMP_DLL = LoadLibraryW(L"Iphlpapi.dll");
    if (!hICMP_DLL) {
        MessageBoxW(nullptr, L"Failed to load Iphlpapi.dll!", L"OpenMTR", MB_OK | MB_ICONERROR);
        return;
    }

    // IPv4 function pointers
    lpfnIcmpCreateFile  = (LPFNICMPCREATEFILE) GetProcAddress(hICMP_DLL, "IcmpCreateFile");
    lpfnIcmpCloseHandle = (LPFNICMPCLOSEHANDLE)GetProcAddress(hICMP_DLL, "IcmpCloseHandle");
    lpfnIcmpSendEcho2   = (LPFNICMPSENDECHO2)  GetProcAddress(hICMP_DLL, "IcmpSendEcho2");
    if (!lpfnIcmpCreateFile || !lpfnIcmpCloseHandle || !lpfnIcmpSendEcho2) {
        MessageBoxW(nullptr, L"Wrong ICMP system library!", L"OpenMTR", MB_OK | MB_ICONERROR);
        return;
    }

    // IPv6 function pointers (soft fail — app still works for IPv4)
    lpfnIcmp6CreateFile = (LPFNICMP6CREATEFILE)GetProcAddress(hICMP_DLL, "Icmp6CreateFile");
    lpfnIcmp6SendEcho2  = (LPFNICMP6SENDECHO2) GetProcAddress(hICMP_DLL, "Icmp6SendEcho2");
    hasIPv6 = (lpfnIcmp6CreateFile != nullptr && lpfnIcmp6SendEcho2 != nullptr);

    hICMP = lpfnIcmpCreateFile();
    if (hICMP == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr, L"Error opening ICMP handle!", L"OpenMTR", MB_OK | MB_ICONERROR);
        return;
    }

    if (hasIPv6) {
        hICMP6 = lpfnIcmp6CreateFile();
        if (hICMP6 == INVALID_HANDLE_VALUE) {
            hasIPv6 = false; // soft fail
        }
    }

    initialized = true;
}

WinMTRNet::~WinMTRNet()
{
    if (initialized) {
        if (hasIPv6 && hICMP6 != INVALID_HANDLE_VALUE)
            lpfnIcmpCloseHandle(hICMP6);
        if (hICMP != INVALID_HANDLE_VALUE)
            lpfnIcmpCloseHandle(hICMP);
        FreeLibrary(hICMP_DLL);
        // WSACleanup is called once globally in main.cpp
    }
    if (ghMutex) CloseHandle(ghMutex);
}

void WinMTRNet::ResetHops()
{
    memset(host, 0, sizeof(host));
}

// ── DoTrace — spins one thread per TTL, blocks until StopTrace() ─────────────

void WinMTRNet::DoTrace(sockaddr* dest)
{
    HANDLE hThreads[MAX_HOPS];
    unsigned char hops = 0;
    tracing = true;
    ResetHops();

    if (dest->sa_family == AF_INET6) {
        host[0].addr6.sin6_family = AF_INET6;
        last_remote_addr6 = ((sockaddr_in6*)dest)->sin6_addr;

        for (; hops < MAX_HOPS;) {
            auto* t = new trace_thread6;
            t->address = *(sockaddr_in6*)dest;
            t->winmtr  = this;
            t->ttl     = hops + 1;
            hThreads[hops] = (HANDLE)_beginthreadex(nullptr, 0, TraceThread6, t, 0, nullptr);
            Sleep(5);
            ++hops;
        }
    } else {
        host[0].addr.sin_family = AF_INET;
        last_remote_addr = ((sockaddr_in*)dest)->sin_addr;

        for (; hops < MAX_HOPS;) {
            auto* t = new trace_thread;
            t->address = ((sockaddr_in*)dest)->sin_addr;
            t->winmtr  = this;
            t->ttl     = hops + 1;
            hThreads[hops] = (HANDLE)_beginthreadex(nullptr, 0, TraceThread, t, 0, nullptr);
            Sleep(5);
            ++hops;
        }
    }

    WaitForMultipleObjects(hops, hThreads, TRUE, INFINITE);
    for (; hops;) CloseHandle(hThreads[--hops]);
    tracing = false;
}

void WinMTRNet::StopTrace()
{
    tracing = false;
}

// ── Trace threads ─────────────────────────────────────────────────────────────

static unsigned WINAPI TraceThread(void* p)
{
    auto* current  = (trace_thread*)p;
    WinMTRNet* wn  = current->winmtr;
    WORD nDataLen  = (WORD)wn->opts.pingsize;

    IPINFO stIPInfo = {};
    stIPInfo.Ttl   = (UCHAR)current->ttl;
    stIPInfo.Flags = IPFLAG_DONT_FRAGMENT;

    char achReqData[8192];
    memset(achReqData, 32, sizeof(achReqData)); // fill with spaces

    union {
        ICMP_ECHO_REPLY icmp_echo_reply;
        char achRepData[sizeof(ICMPECHO) + 8192];
    };

    while (wn->tracing) {
        if (current->ttl > wn->GetMax()) break;

        DWORD dwReplyCount = wn->lpfnIcmpSendEcho2(
            wn->hICMP, nullptr, nullptr, nullptr,
            current->address, achReqData, nDataLen,
            &stIPInfo, achRepData, sizeof(achRepData),
            ECHO_REPLY_TIMEOUT);

        wn->AddXmit(current->ttl - 1);

        if (dwReplyCount) {
            switch (icmp_echo_reply.Status) {
            case IP_SUCCESS:
            case IP_TTL_EXPIRED_TRANSIT:
                wn->UpdateRTT(current->ttl - 1, icmp_echo_reply.RoundTripTime);
                wn->AddReturned(current->ttl - 1);
                wn->SetAddr(current->ttl - 1, icmp_echo_reply.Address);
                break;
            default:
                wn->SetErrorName(current->ttl - 1, icmp_echo_reply.Status);
                break;
            }
            DWORD sleepMs = (DWORD)(wn->opts.interval * 1000);
            if (sleepMs > icmp_echo_reply.RoundTripTime)
                Sleep(sleepMs - icmp_echo_reply.RoundTripTime);
        } else {
            DWORD err = GetLastError();
            wn->SetErrorName(current->ttl - 1, err);
            if (err != IP_REQ_TIMED_OUT)
                Sleep((DWORD)(wn->opts.interval * 1000));
        }
    }

    delete current;
    return 0;
}

static unsigned WINAPI TraceThread6(void* p)
{
    static sockaddr_in6 from = { AF_INET6, 0, 0, in6addr_any, 0 };
    auto* current  = (trace_thread6*)p;
    WinMTRNet* wn  = current->winmtr;
    WORD nDataLen  = (WORD)wn->opts.pingsize;

    IPINFO stIPInfo = {};
    stIPInfo.Ttl   = (UCHAR)current->ttl;
    stIPInfo.Flags = IPFLAG_DONT_FRAGMENT;

    char achReqData[8192];
    memset(achReqData, 32, sizeof(achReqData));

    union {
        ICMPV6_ECHO_REPLY icmpv6_echo_reply;
        char achRepData[sizeof(PICMPV6_ECHO_REPLY) + 8192];
    };

    while (wn->tracing) {
        if (current->ttl > wn->GetMax()) break;

        DWORD dwReplyCount = wn->lpfnIcmp6SendEcho2(
            wn->hICMP6, nullptr, nullptr, nullptr,
            &from, &current->address,
            achReqData, nDataLen,
            &stIPInfo, achRepData, sizeof(achRepData),
            ECHO_REPLY_TIMEOUT);

        wn->AddXmit(current->ttl - 1);

        if (dwReplyCount) {
            switch (icmpv6_echo_reply.Status) {
            case IP_SUCCESS:
            case IP_TTL_EXPIRED_TRANSIT:
                wn->UpdateRTT(current->ttl - 1, icmpv6_echo_reply.RoundTripTime);
                wn->AddReturned(current->ttl - 1);
                wn->SetAddr6(current->ttl - 1, icmpv6_echo_reply.Address);
                break;
            default:
                wn->SetErrorName(current->ttl - 1, icmpv6_echo_reply.Status);
                break;
            }
            DWORD sleepMs = (DWORD)(wn->opts.interval * 1000);
            if (sleepMs > icmpv6_echo_reply.RoundTripTime)
                Sleep(sleepMs - icmpv6_echo_reply.RoundTripTime);
        } else {
            DWORD err = GetLastError();
            wn->SetErrorName(current->ttl - 1, err);
            if (err != IP_REQ_TIMED_OUT)
                Sleep((DWORD)(wn->opts.interval * 1000));
        }
    }

    delete current;
    return 0;
}

// ── DNS resolver ──────────────────────────────────────────────────────────────

static void DnsResolverThread(void* p)
{
    auto* dnt     = (dns_resolver_thread*)p;
    WinMTRNet* wn = dnt->winmtr;
    char hostname[NI_MAXHOST];

    // First set the numeric IP immediately
    if (!getnameinfo(wn->GetAddr(dnt->index), sizeof(sockaddr_in6),
                     hostname, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST)) {
        wn->SetName(dnt->index, hostname);
    }

    // Then attempt reverse DNS if enabled
    if (wn->opts.useDNS) {
        if (!getnameinfo(wn->GetAddr(dnt->index), sizeof(sockaddr_in6),
                         hostname, NI_MAXHOST, nullptr, 0, 0)) {
            wn->SetName(dnt->index, hostname);
        }
    }

    delete dnt;
}

// ── Getters (all mutex-guarded) ───────────────────────────────────────────────

sockaddr* WinMTRNet::GetAddr(int at)
{
    return (sockaddr*)&host[at].addr;
}

int WinMTRNet::GetName(int at, char* n)
{
    WaitForSingleObject(ghMutex, INFINITE);
    strcpy_s(n, NI_MAXHOST, host[at].name);
    ReleaseMutex(ghMutex);
    return 0;
}

int WinMTRNet::GetBest(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].best;
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetWorst(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].worst;
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetAvg(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].returned == 0 ? 0 : (int)(host[at].total / host[at].returned);
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetPercent(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].xmit == 0 ? 0 : (100 - (100 * host[at].returned / host[at].xmit));
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetLast(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].last;
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetReturned(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].returned;
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetXmit(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    int r = host[at].xmit;
    ReleaseMutex(ghMutex);
    return r;
}

int WinMTRNet::GetMax()
{
    WaitForSingleObject(ghMutex, INFINITE);
    int max = 0;
    if (host[0].addr6.sin6_family == AF_INET6) {
        for (; max < MAX_HOPS && memcmp(&host[max++].addr6.sin6_addr, &last_remote_addr6, sizeof(in6_addr)););
        if (max == MAX_HOPS) {
            while (max > 1
                && !memcmp(&host[max-1].addr6.sin6_addr, &host[max-2].addr6.sin6_addr, sizeof(in6_addr))
                && (host[max-1].addr6.sin6_addr.u.Word[0] | host[max-1].addr6.sin6_addr.u.Word[1]
                  | host[max-1].addr6.sin6_addr.u.Word[2] | host[max-1].addr6.sin6_addr.u.Word[3]
                  | host[max-1].addr6.sin6_addr.u.Word[4] | host[max-1].addr6.sin6_addr.u.Word[5]
                  | host[max-1].addr6.sin6_addr.u.Word[6] | host[max-1].addr6.sin6_addr.u.Word[7]))
                --max;
        }
    } else {
        for (; max < MAX_HOPS && host[max++].addr.sin_addr.s_addr != last_remote_addr.s_addr;);
        if (max == MAX_HOPS) {
            while (max > 1
                && host[max-1].addr.sin_addr.s_addr == host[max-2].addr.sin_addr.s_addr
                && host[max-1].addr.sin_addr.s_addr)
                --max;
        }
    }
    ReleaseMutex(ghMutex);
    return max;
}

// ── Setters ───────────────────────────────────────────────────────────────────

void WinMTRNet::SetAddr(int at, u_long addr)
{
    WaitForSingleObject(ghMutex, INFINITE);
    if (host[at].addr.sin_addr.s_addr == 0) {
        host[at].addr.sin_family     = AF_INET;
        host[at].addr.sin_addr.s_addr = addr;
        auto* dnt   = new dns_resolver_thread;
        dnt->index  = at;
        dnt->winmtr = this;
        if (opts.useDNS) _beginthread(DnsResolverThread, 0, dnt);
        else             DnsResolverThread(dnt);
    }
    ReleaseMutex(ghMutex);
}

void WinMTRNet::SetAddr6(int at, IPV6_ADDRESS_EX addrex)
{
    WaitForSingleObject(ghMutex, INFINITE);
    const auto& w = host[at].addr6.sin6_addr.u.Word;
    bool empty = !(w[0]|w[1]|w[2]|w[3]|w[4]|w[5]|w[6]|w[7]);
    if (empty) {
        host[at].addr6.sin6_family = AF_INET6;
        host[at].addr6.sin6_addr   = *(in6_addr*)&addrex.sin6_addr;
        auto* dnt   = new dns_resolver_thread;
        dnt->index  = at;
        dnt->winmtr = this;
        if (opts.useDNS) _beginthread(DnsResolverThread, 0, dnt);
        else             DnsResolverThread(dnt);
    }
    ReleaseMutex(ghMutex);
}

void WinMTRNet::SetName(int at, char* n)
{
    WaitForSingleObject(ghMutex, INFINITE);
    strcpy_s(host[at].name, sizeof(host[at].name), n);
    ReleaseMutex(ghMutex);
}

void WinMTRNet::SetErrorName(int at, DWORD errnum)
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
    WaitForSingleObject(ghMutex, INFINITE);
    if (!*host[at].name)
        strcpy_s(host[at].name, sizeof(host[at].name), name);
    ReleaseMutex(ghMutex);
}

void WinMTRNet::UpdateRTT(int at, int rtt)
{
    WaitForSingleObject(ghMutex, INFINITE);
    host[at].last   = rtt;
    host[at].total += rtt;
    if (host[at].returned == 0 || host[at].best > rtt) host[at].best = rtt;
    if (host[at].worst < rtt)                       host[at].worst = rtt;
    ReleaseMutex(ghMutex);
}

void WinMTRNet::AddReturned(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    ++host[at].returned;
    ReleaseMutex(ghMutex);
}

void WinMTRNet::AddXmit(int at)
{
    WaitForSingleObject(ghMutex, INFINITE);
    ++host[at].xmit;
    ReleaseMutex(ghMutex);
}
