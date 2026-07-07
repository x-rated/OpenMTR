#pragma once

//*****************************************************************************
// OpenMTRNet.h — White-Tiger WinMTR Redux engine, de-MFC'd for Qt6
//*****************************************************************************

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windows.h>
#include <string>

#define MAX_HOPS        30
#define ECHO_REPLY_TIMEOUT 1000

typedef IP_OPTION_INFORMATION IPINFO, *PIPINFO, FAR* LPIPINFO;
#ifdef _WIN64
typedef ICMP_ECHO_REPLY32 ICMPECHO, *PICMPECHO, FAR* LPICMPECHO;
#else
typedef ICMP_ECHO_REPLY ICMPECHO, *PICMPECHO, FAR* LPICMPECHO;
#endif

// Options passed in from the UI
struct OpenMTROptions {
    unsigned pingsize = 64;
    double   interval = 1.0;  // seconds between pings per hop
    bool     useDNS   = true;
};

struct s_nethost {
    union {
        sockaddr_in  addr;
        sockaddr_in6 addr6;
    };
    int           xmit;      // packets sent
    int           returned;  // echo replies received
    unsigned long total;     // total RTT
    int           last;      // last RTT
    int           best;      // best RTT
    int           worst;     // worst RTT
    char          name[256];
};

class OpenMTRNet
{
    typedef FARPROC PIO_APC_ROUTINE;
    // IPv4
    typedef HANDLE(WINAPI* LPFNICMPCREATEFILE)(VOID);
    typedef BOOL  (WINAPI* LPFNICMPCLOSEHANDLE)(HANDLE);
    typedef DWORD (WINAPI* LPFNICMPSENDECHO2)(HANDLE,HANDLE,PIO_APC_ROUTINE,PVOID,in_addr,LPVOID,WORD,PIP_OPTION_INFORMATION,LPVOID,DWORD,DWORD);
    // IPv6
    typedef HANDLE(WINAPI* LPFNICMP6CREATEFILE)(VOID);
    typedef DWORD (WINAPI* LPFNICMP6SENDECHO2)(HANDLE,HANDLE,PIO_APC_ROUTINE,PVOID,sockaddr_in6*,sockaddr_in6*,LPVOID,WORD,PIP_OPTION_INFORMATION,LPVOID,DWORD,DWORD);

public:
    explicit OpenMTRNet(const OpenMTROptions& opts);
    ~OpenMTRNet();

    // Main trace loop — blocks until StopTrace() is called
    void DoTrace(sockaddr* dest);
    void StopTrace();
    void ResetHops();

    // Per-hop getters (thread-safe)
    sockaddr* GetAddr(int at);
    int  GetName(int at, char* n);
    int  GetBest(int at);
    int  GetWorst(int at);
    int  GetAvg(int at);
    int  GetPercent(int at);
    int  GetLast(int at);
    int  GetReturned(int at);
    int  GetXmit(int at);
    int  GetMax();

    // Called from trace threads
    void SetAddr(int at, u_long addr);
    void SetAddr6(int at, IPV6_ADDRESS_EX addrex);
    void SetName(int at, char* n);
    void SetErrorName(int at, DWORD errnum);
    void UpdateRTT(int at, int rtt);
    void AddReturned(int at);
    void AddXmit(int at);

    // State
    bool  tracing     = false;
    bool  hasIPv6     = false;
    bool  initialized = false;

    union {
        in_addr  last_remote_addr;
        in6_addr last_remote_addr6;
    };

    OpenMTROptions opts;

    HANDLE hICMP  = INVALID_HANDLE_VALUE;
    HANDLE hICMP6 = INVALID_HANDLE_VALUE;

    LPFNICMPCREATEFILE  lpfnIcmpCreateFile  = nullptr;
    LPFNICMPCLOSEHANDLE lpfnIcmpCloseHandle = nullptr;
    LPFNICMPSENDECHO2   lpfnIcmpSendEcho2   = nullptr;
    LPFNICMP6CREATEFILE lpfnIcmp6CreateFile = nullptr;
    LPFNICMP6SENDECHO2  lpfnIcmp6SendEcho2  = nullptr;

private:
    HINSTANCE    hICMP_DLL = nullptr;
    s_nethost    host[MAX_HOPS];
    HANDLE       ghMutex   = nullptr;
};

// ── Wrapper (formerly OpenMTRNetWrapper.h) ───────────────────────────────────
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <stop_token>
#include <memory>
#include <chrono>


// ── Options interface — defined here so both MainWindow.h and
//    OpenMTRNetWrapper.h see the same declaration ────────────────────────────
struct IOpenMTROptionsProvider {
    virtual ~IOpenMTROptionsProvider() = default;
    [[nodiscard]] virtual unsigned getPingSize() const noexcept = 0;
    [[nodiscard]] virtual double   getInterval() const noexcept = 0;
    [[nodiscard]] virtual bool     getUseDNS()   const noexcept = 0;
};

// ── Hop snapshot ─────────────────────────────────────────────────────────────
struct OpenMTRHostInfo {
    SOCKADDR_INET addr = {};
    int           xmit     = 0;
    int           returned = 0;
    int           best     = 0;
    int           worst    = 0;
    int           last     = 0;
    unsigned long total    = 0;
    std::wstring  _name;

    std::wstring getName() const { return _name; }
    int getAvg() const {
        return (returned == 0) ? 0 : static_cast<int>(total / returned);
    }
};

// ── IP address → wstring ─────────────────────────────────────────────────────
inline std::wstring addr_to_wstring(const SOCKADDR_INET& addr)
{
    wchar_t buf[64] = {};
    if      (addr.si_family == AF_INET)
        InetNtopW(AF_INET,  &addr.Ipv4.sin_addr,  buf, 64);
    else if (addr.si_family == AF_INET6)
        InetNtopW(AF_INET6, &addr.Ipv6.sin6_addr, buf, 64);
    return buf;
}

// ── Wrapper ──────────────────────────────────────────────────────────────────
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
        opts.interval = m_provider->getInterval();
        opts.useDNS   = m_provider->getUseDNS();

        m_net = std::make_unique<OpenMTRNet>(opts);

        sockaddr_storage ss = {};
        if (dest.si_family == AF_INET) {
            auto* s4 = (sockaddr_in*)&ss;
            s4->sin_family = AF_INET;
            s4->sin_addr   = dest.Ipv4.sin_addr;
        } else {
            auto* s6 = (sockaddr_in6*)&ss;
            s6->sin6_family = AF_INET6;
            s6->sin6_addr   = dest.Ipv6.sin6_addr;
        }

        m_thread = std::thread([this, ss, stopToken]() mutable {
            std::thread watcher([this, &stopToken]() {
                while (!stopToken.stop_requested() && !m_done.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (m_net) m_net->StopTrace();
            });

            m_net->DoTrace((sockaddr*)&ss);
            m_done.store(true);

            if (watcher.joinable()) watcher.join();
        });

        return 0;
    }

    std::vector<OpenMTRHostInfo> getCurrentState() const
    {
        std::vector<OpenMTRHostInfo> state;
        if (!m_net) return state;

        int maxHops = m_net->GetMax();
        int rows = (maxHops > 0 && maxHops <= MAX_HOPS) ? maxHops : MAX_HOPS;

        for (int i = 0; i < rows; ++i) {
            OpenMTRHostInfo h;
            sockaddr* sa = m_net->GetAddr(i);
            if (sa->sa_family == AF_INET) {
                h.addr.si_family = AF_INET;
                h.addr.Ipv4      = *(sockaddr_in*)sa;
            } else if (sa->sa_family == AF_INET6) {
                h.addr.si_family = AF_INET6;
                h.addr.Ipv6      = *(sockaddr_in6*)sa;
            } else {
                h.addr.si_family = AF_UNSPEC;
            }

            h.xmit     = m_net->GetXmit(i);
            h.returned = m_net->GetReturned(i);
            h.best     = m_net->GetBest(i);
            h.worst    = m_net->GetWorst(i);
            h.last     = m_net->GetLast(i);
            h.total    = (unsigned long)(h.returned > 0
                            ? (long long)m_net->GetAvg(i) * h.returned : 0);

            char nameBuf[256] = {};
            m_net->GetName(i, nameBuf);
            if (nameBuf[0])
                h._name = std::wstring(nameBuf, nameBuf + strlen(nameBuf));
            else if (h.addr.si_family != AF_UNSPEC)
                h._name = addr_to_wstring(h.addr);

            state.push_back(h);
        }
        return state;
    }

    bool isDone()  const { return m_done.load(); }
    int  GetMax()  const { return m_net ? m_net->GetMax() : MAX_HOPS; }

private:
    IOpenMTROptionsProvider*    m_provider;
    std::unique_ptr<OpenMTRNet> m_net;
    std::thread                m_thread;
    std::atomic<bool>          m_done{ true };
};
