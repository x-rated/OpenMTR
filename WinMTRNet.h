#pragma once

//*****************************************************************************
// WinMTRNet.h — White-Tiger WinMTR Redux engine, de-MFC'd for Qt6
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
#define ECHO_REPLY_TIMEOUT 1500

typedef IP_OPTION_INFORMATION IPINFO, *PIPINFO, FAR* LPIPINFO;
#ifdef _WIN64
typedef ICMP_ECHO_REPLY32 ICMPECHO, *PICMPECHO, FAR* LPICMPECHO;
#else
typedef ICMP_ECHO_REPLY ICMPECHO, *PICMPECHO, FAR* LPICMPECHO;
#endif

// Options passed in from the UI — replaces WinMTRDialog* dependency
struct WinMTROptions {
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

class WinMTRNet
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
    explicit WinMTRNet(const WinMTROptions& opts);
    ~WinMTRNet();

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

    WinMTROptions opts;

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
