#pragma once

//*****************************************************************************
// WinMTRNetWrapper.h
//
// Adapts White-Tiger's WinMTRNet (classic Win32 threads) to the interface
// that MainWindow.cpp expects:
//
//   m_net->DoTrace(stop_token, SOCKADDR_INET)   — start trace on bg thread
//   m_net->getCurrentState()                    — snapshot of all hops
//   m_net->isDone()                             — true when trace thread exited
//   m_net->GetMax()                             — current hop count
//
// MainWindow.cpp is NOT modified at all.
//*****************************************************************************

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <windns.h>
#include <windows.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <stop_token>
#include <memory>

#include "WinMTRNet.h"

// ── The hop snapshot struct MainWindow.cpp reads every second ────────────────
// Mirrors what the old C++20 engine exposed via getCurrentState()

struct WinMTRHostInfo {
    SOCKADDR_INET addr;    // address (si_family AF_INET or AF_INET6 or AF_UNSPEC)
    int           xmit;
    int           returned;
    int           best;
    int           worst;
    int           last;
    unsigned long total;
    std::wstring  _name;   // cached name for getName()

    std::wstring getName() const { return _name; }
};

// ── Helper: wstring IP from SOCKADDR_INET ────────────────────────────────────

inline std::wstring addr_to_wstring(const SOCKADDR_INET& addr)
{
    wchar_t buf[64] = {};
    if (addr.si_family == AF_INET)
        InetNtopW(AF_INET,  &addr.Ipv4.sin_addr,  buf, 64);
    else if (addr.si_family == AF_INET6)
        InetNtopW(AF_INET6, &addr.Ipv6.sin6_addr, buf, 64);
    return buf;
}

// ── Wrapper class ─────────────────────────────────────────────────────────────

class WinMTRNet;  // forward (defined in WinMTRNet.h)

class WinMTRNetWrapper
{
public:
    // IWinMTROptionsProvider equivalent — MainWindow sets these via constructor
    explicit WinMTRNetWrapper(class IWinMTROptionsProvider* provider)
        : m_provider(provider)
    {}

    ~WinMTRNetWrapper()
    {
        // Signal stop and wait for the trace thread to finish
        if (m_thread.joinable()) {
            if (m_net) m_net->StopTrace();
            m_thread.join();
        }
    }

    // Called by MainWindow to kick off a trace.
    // stop_token is monitored — when stop is requested we call StopTrace().
    // Returns a dummy future-like object (MainWindow just discards it with [[maybe_unused]]).
    int DoTrace(std::stop_token stopToken, SOCKADDR_INET dest)
    {
        m_done.store(false);

        // Build options from the provider
        WinMTROptions opts;
        opts.pingsize = m_provider->getPingSize();
        opts.interval = m_provider->getInterval();
        opts.useDNS   = m_provider->getUseDNS();

        m_net = std::make_unique<WinMTRNet>(opts);

        // Convert SOCKADDR_INET → sockaddr for White-Tiger's DoTrace
        sockaddr_storage ss = {};
        sockaddr* psa = (sockaddr*)&ss;
        if (dest.si_family == AF_INET) {
            auto* s4 = (sockaddr_in*)&ss;
            s4->sin_family = AF_INET;
            s4->sin_addr   = dest.Ipv4.sin_addr;
        } else {
            auto* s6 = (sockaddr_in6*)&ss;
            s6->sin6_family = AF_INET6;
            s6->sin6_addr   = dest.Ipv6.sin6_addr;
        }

        // Launch trace on a background thread so DoTrace() returns immediately
        m_thread = std::thread([this, psa = ss, stopToken]() mutable {
            // Watch stop_token in a separate watcher thread
            std::thread watcher([this, &stopToken]() {
                while (!stopToken.stop_requested() && !m_done.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (m_net) m_net->StopTrace();
            });

            m_net->DoTrace((sockaddr*)&psa);
            m_done.store(true);

            if (watcher.joinable()) watcher.join();
        });

        return 0; // MainWindow discards return value
    }

    // Returns a snapshot of all hops — called every second by MainWindow's refresh timer
    std::vector<WinMTRHostInfo> getCurrentState() const
    {
        std::vector<WinMTRHostInfo> state;
        if (!m_net) return state;

        int maxHops = m_net->GetMax();
        // Always return MAX_HOPS rows so table size is stable, same as old engine
        int rows = (maxHops > 0 && maxHops <= MAX_HOPS) ? maxHops : MAX_HOPS;

        for (int i = 0; i < rows; ++i) {
            WinMTRHostInfo h = {};

            // Build SOCKADDR_INET from s_nethost
            sockaddr* sa = m_net->GetAddr(i);
            if (sa->sa_family == AF_INET) {
                h.addr.si_family        = AF_INET;
                h.addr.Ipv4             = *(sockaddr_in*)sa;
            } else if (sa->sa_family == AF_INET6) {
                h.addr.si_family        = AF_INET6;
                h.addr.Ipv6             = *(sockaddr_in6*)sa;
            } else {
                h.addr.si_family        = AF_UNSPEC;
            }

            h.xmit     = m_net->GetXmit(i);
            h.returned = m_net->GetReturned(i);
            h.best     = m_net->GetBest(i);
            h.worst    = m_net->GetWorst(i);
            h.last     = m_net->GetLast(i);
            h.total    = (unsigned long)(h.returned > 0 ? (long long)m_net->GetAvg(i) * h.returned : 0);

            // Name: prefer resolved hostname, fall back to numeric IP
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

    // True once the trace thread has fully exited
    bool isDone() const { return m_done.load(); }

    // Current hop count
    int GetMax() const { return m_net ? m_net->GetMax() : MAX_HOPS; }

private:
    class IWinMTROptionsProvider* m_provider;
    std::unique_ptr<WinMTRNet>    m_net;
    std::thread                   m_thread;
    std::atomic<bool>             m_done{ true };
};
