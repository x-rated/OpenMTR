// ==========================================================================
//  engine_tests.cpp - tests for the tracer engine alone (no Qt, no window).
//  Nothing here sends a packet off the machine.
//  Usage: engine_tests <case>. Exit status 0 = pass.
// ==========================================================================
#include "tracer.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

#ifdef __linux__
#include <sys/resource.h>
#endif

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// MainWindow prints these numbers into the hop tooltip and the export, so on
// POSIX every one of them must stay the Windows SDK's (ipexport.h) value.
static_assert(IP_BUF_TOO_SMALL == 11001 && IP_DEST_NET_UNREACHABLE == 11002
              && IP_DEST_HOST_UNREACHABLE == 11003 && IP_DEST_PROT_UNREACHABLE == 11004
              && IP_DEST_PORT_UNREACHABLE == 11005 && IP_NO_RESOURCES == 11006
              && IP_BAD_OPTION == 11007 && IP_HW_ERROR == 11008
              && IP_PACKET_TOO_BIG == 11009 && IP_REQ_TIMED_OUT == 11010
              && IP_BAD_REQ == 11011 && IP_BAD_ROUTE == 11012
              && IP_TTL_EXPIRED_TRANSIT == 11013 && IP_TTL_EXPIRED_REASSEM == 11014
              && IP_PARAM_PROBLEM == 11015 && IP_SOURCE_QUENCH == 11016
              && IP_OPTION_TOO_BIG == 11017 && IP_BAD_DESTINATION == 11018
              && IP_GENERAL_FAILURE == 11050,
              "IP_* status values must match the Windows SDK");

// An engine destroyed while a reverse-DNS worker it started is still running.
// That is the use-after-free 1.3.0 shipped after the fix for it was lost in
// an upload (0f67645): against that tree this aborts ("mutex lock failed") or
// hangs, which is what the ctest TIMEOUT is for. useDNS = false keeps every
// worker on getnameinfo(NI_NUMERICHOST), so no resolver is involved.
static void dns_lifetime()
{
    for (int i = 0; i < 5000; ++i) {
        OpenMTROptions o;
        o.useDNS = false;
        auto net = std::make_unique<OpenMTRNet>(o);
        net->SetAddr(0, htonl(0x7F000001u + static_cast<unsigned>(i & 0xFF)));
        net.reset();
    }
    // Give the last workers time to find their engine gone before exit.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

static std::string nameOf(OpenMTRNet& n, int hop)
{
    return n.GetHopSnapshot(hop).name;
}

// SetErrorName() only fills an empty name, except that a "Not sent" text is
// provisional: the next status that is not local, or a resolved name,
// replaces it.
static void status_text()
{
    OpenMTROptions o;
    o.useDNS = false;
    OpenMTRNet n(o);
    char resolved[] = "router.example";

    n.SetErrorName(0, OPENMTR_NOT_SENT_NO_ROUTE);
    CHECK(nameOf(n, 0) == "Not sent: no route from this machine.");
    n.SetErrorName(0, OPENMTR_NOT_SENT_REFUSED);           // first local text stays
    CHECK(nameOf(n, 0) == "Not sent: no route from this machine.");
    n.SetErrorName(0, IP_REQ_TIMED_OUT);                   // a real status replaces it
    CHECK(nameOf(n, 0) == "Request timed out.");
    n.SetErrorName(0, OPENMTR_NOT_SENT_NO_ROUTE);          // ...and is not replaced back
    CHECK(nameOf(n, 0) == "Request timed out.");
    n.SetErrorName(0, IP_DEST_HOST_UNREACHABLE);           // first real status stays
    CHECK(nameOf(n, 0) == "Request timed out.");

    n.SetErrorName(1, OPENMTR_NOT_SENT_TOO_BIG);
    n.SetName(1, resolved);                                // a name replaces it
    CHECK(nameOf(n, 1) == "router.example");
    n.SetErrorName(1, OPENMTR_NOT_SENT_OTHER);             // and a name is never replaced
    n.SetErrorName(1, IP_REQ_TIMED_OUT);
    CHECK(nameOf(n, 1) == "router.example");

    // Every error status in the IP_* table has a sentence of its own.
    // IP_TTL_EXPIRED_TRANSIT is left out: a hop answering with it is a
    // normal reply, never an error.
    const unsigned long errors[] = {
        IP_BUF_TOO_SMALL, IP_DEST_NET_UNREACHABLE, IP_DEST_HOST_UNREACHABLE,
        IP_DEST_PROT_UNREACHABLE, IP_DEST_PORT_UNREACHABLE, IP_NO_RESOURCES,
        IP_BAD_OPTION, IP_HW_ERROR, IP_PACKET_TOO_BIG, IP_REQ_TIMED_OUT, IP_BAD_REQ,
        IP_BAD_ROUTE, IP_TTL_EXPIRED_REASSEM, IP_PARAM_PROBLEM, IP_SOURCE_QUENCH,
        IP_OPTION_TOO_BIG, IP_BAD_DESTINATION, IP_GENERAL_FAILURE };
    for (unsigned long st : errors)
        CHECK(std::string(OpenMTRStatusText(st, false)) != "Unknown error.");
    CHECK(std::string(OpenMTRStatusText(OPENMTR_STATUS_BASE, false)) == "Unknown error.");
    // 11004 means something else for IPv6 on Windows (IP_DEST_PROHIBITED).
    CHECK(std::string(OpenMTRStatusText(IP_DEST_PROT_UNREACHABLE, false))
          == "Destination protocol unreachable.");
    CHECK(std::string(OpenMTRStatusText(IP_DEST_PROT_UNREACHABLE, true))
          == "Communication administratively prohibited.");

    const DWORD notSent[] = { OPENMTR_NOT_SENT_NO_ROUTE, OPENMTR_NOT_SENT_NO_ADDRESS,
                              OPENMTR_NOT_SENT_TOO_BIG, OPENMTR_NOT_SENT_NO_BUFFERS,
                              OPENMTR_NOT_SENT_REFUSED, OPENMTR_NOT_SENT_OTHER };
    int hop = 2;
    for (DWORD st : notSent) {
        n.SetErrorName(hop, st);
        CHECK(nameOf(n, hop).rfind("Not sent: ", 0) == 0);
        ++hop;
    }
}

// The ICMPv4 checksum as the bytes that go on the wire: RFC 1071's own
// example, the same with one more byte, and an echo request with an odd
// payload added up the way a receiver does. Every byte order must produce
// the same bytes; an odd length is what a big-endian CPU got wrong, so on a
// little-endian machine this passed before the fix too.
static void icmp_checksum()
{
    const unsigned char even[] = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7 };
    const unsigned char odd[]  = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7, 0xa5 };
    unsigned char wire[2];

    uint16_t sum = OpenMTRIcmpChecksum(even, sizeof(even));
    std::memcpy(wire, &sum, sizeof(wire));
    CHECK(wire[0] == 0x22 && wire[1] == 0x0d);

    sum = OpenMTRIcmpChecksum(odd, sizeof(odd));
    std::memcpy(wire, &sum, sizeof(wire));
    CHECK(wire[0] == 0x7d && wire[1] == 0x0c);

    unsigned char pkt[8 + 65];
    std::memset(pkt, ' ', sizeof(pkt));
    const unsigned char header[8] = { 8, 0, 0, 0, 0x12, 0x34, 0x08, 0x01 };   // echo request, checksum 0
    std::memcpy(pkt, header, sizeof(header));
    sum = OpenMTRIcmpChecksum(pkt, sizeof(pkt));
    std::memcpy(pkt + 2, &sum, sizeof(sum));
    uint32_t total = 0;
    for (size_t i = 0; i < sizeof(pkt); i += 2)
        total += (static_cast<uint32_t>(pkt[i]) << 8) | (i + 1 < sizeof(pkt) ? pkt[i + 1] : 0u);
    while (total >> 16)
        total = (total >> 16) + (total & 0xFFFF);
    CHECK(total == 0xFFFF);
}

// A PTR record can hold any bytes. SetName() turns control characters into
// '?', so that a name cannot start a new CSV row or a terminal escape
// sequence; everything else, bytes above 0x7F included, stays.
static void name_control_chars()
{
    OpenMTROptions o;
    o.useDNS = false;
    OpenMTRNet n(o);

    char hostile[] = "a\rb\n=1+1\x1b[31m\x7f\tend.example";
    n.SetName(0, hostile);
    CHECK(nameOf(n, 0) == "a?b?=1+1?[31m??end.example");

    char plain[] = "router-1.example.net";
    n.SetName(1, plain);
    CHECK(nameOf(n, 1) == "router-1.example.net");

    char utf8[] = "b\xc3\xbc" "cher.example";
    n.SetName(2, utf8);
    CHECK(nameOf(n, 2) == "b\xc3\xbc" "cher.example");
}

// SetAddr() starts a reverse-DNS thread for a hop's new address. With no
// thread to be had std::thread throws, which used to end the process; now
// the hop keeps its address and gets no name. Linux counts threads against
// RLIMIT_NPROC (though not for root), which makes the failure easy to cause
// there; elsewhere the case is skipped.
static int no_thread_for_dns()
{
#ifdef __linux__
    if (::geteuid() == 0) {
        std::printf("skipped: root is not held to RLIMIT_NPROC\n");
        return 77;
    }
    struct rlimit before{};
    if (::getrlimit(RLIMIT_NPROC, &before) != 0)
        return 77;
    OpenMTROptions o;
    o.useDNS = false;
    OpenMTRNet n(o);

    struct rlimit none = before;
    none.rlim_cur = 0;
    if (::setrlimit(RLIMIT_NPROC, &none) != 0)
        return 77;
    bool refused = false;                 // does the limit bite here at all?
    try {
        std::thread([] {}).detach();
    } catch (const std::system_error&) {
        refused = true;
    }
    if (refused)
        n.SetAddr(0, htonl(INADDR_LOOPBACK));
    ::setrlimit(RLIMIT_NPROC, &before);
    if (!refused) {
        std::printf("skipped: RLIMIT_NPROC did not refuse a thread here\n");
        return 77;
    }
    CHECK(n.GetHopSnapshot(0).addr.Ipv4.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    CHECK(nameOf(n, 0).empty());
    return 0;
#else
    std::printf("skipped: no way to run out of threads on purpose here\n");
    return 77;
#endif
}

// Whether this process may open an unprivileged ICMP socket of that family
// (on Linux, net.ipv4.ping_group_range decides). Without one, DoTrace()
// returns at once, and a test built on a trace would pass without testing
// anything — so such a test reports itself skipped instead.
static bool pingSocketAvailable(int family)
{
    const int proto = family == AF_INET6 ? static_cast<int>(IPPROTO_ICMPV6)
                                         : static_cast<int>(IPPROTO_ICMP);
    const int fd = ::socket(family, SOCK_DGRAM, proto);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

// An IPv6 trace whose first hop never answers must still find the route's
// length. RecalcMaxLocked() used to decide the family from hop 1's address,
// so until hop 1 answered — forever, if it never does — GetMax() stayed at
// MAX_HOPS. fe80::1 without a scope id makes every sendto() fail locally, so
// no hop ever gets an address and nothing leaves the machine; the
// destination is then marked at hop 4 the way a reply would mark it.
static int v6_silent_first_hop()
{
    if (!pingSocketAvailable(AF_INET6)) {
        std::printf("skipped: no unprivileged ICMPv6 socket here\n");
        return 77;
    }
    OpenMTROptions o;
    o.useDNS = false;
    OpenMTRNet net(o);
    sockaddr_in6 dest{};
#ifdef __APPLE__
    dest.sin6_len = sizeof(dest);
#endif
    dest.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "fe80::1", &dest.sin6_addr);
    std::thread trace([&] { net.DoTrace(reinterpret_cast<sockaddr*>(&dest)); });

    // Wait until DoTrace() is past its setup: hop 1 has had a probe.
    const auto t0 = std::chrono::steady_clock::now();
    while (net.GetHopSnapshot(0).xmit == 0
           && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool started = net.GetHopSnapshot(0).xmit > 0;
    CHECK(started);            // the socket opens here, so the trace must run

    if (started) {
        IPV6_ADDRESS_EX hop4{};
        std::memcpy(hop4.sin6_addr, &dest.sin6_addr, sizeof(hop4.sin6_addr));
        net.SetAddr6(3, hop4);
        std::printf("GetMax() = %d\n", net.GetMax());
        CHECK(net.GetMax() == 4);
    }
    net.StopTrace();
    trace.join();
    return 0;
}

static sockaddr_in loopback4()
{
    sockaddr_in d{};
#ifdef __APPLE__
    d.sin_len = sizeof(d);
#endif
    d.sin_family      = AF_INET;
    d.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return d;
}

// A StopTrace() that lands before DoTrace() has started must still end the
// trace. DoTrace() used to set `tracing` itself and so undo it, leaving a
// trace running with nothing left to stop it.
static int stop_before_start()
{
    if (!pingSocketAvailable(AF_INET)) {
        std::printf("skipped: no unprivileged ICMP socket here\n");
        return 77;
    }
    OpenMTROptions o;
    o.useDNS = false;
    OpenMTRNet net(o);
    net.StopTrace();
    sockaddr_in dest = loopback4();
    auto trace = std::async(std::launch::async,
                            [&] { net.DoTrace(reinterpret_cast<sockaddr*>(&dest)); });
    const bool ended = trace.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    CHECK(ended);
    if (!ended)
        net.StopTrace();    // a second stop does get through; lets the test finish
    return 0;
}

// The same race the way the UI hits it: Start, then Stop before the worker
// thread has got as far as DoTrace(). 127.0.0.1 keeps every probe on the
// machine.
static int stop_right_after_start()
{
    if (!pingSocketAvailable(AF_INET)) {
        std::printf("skipped: no unprivileged ICMP socket here\n");
        return 77;
    }
    struct Options : IOpenMTROptionsProvider {
        unsigned getPingSize() const noexcept override { return 64; }
    } options;
    SOCKADDR_INET dest{};
    dest.Ipv4 = loopback4();

    int ignored = 0;
    for (int i = 0; i < 10; ++i) {
        std::stop_source stop;
        auto trace = std::make_unique<OpenMTRNetWrapper>(&options);
        trace->DoTrace(stop.get_token(), dest);
        stop.request_stop();                 // what MainWindow's Stop does
        const auto t0 = std::chrono::steady_clock::now();
        while (!trace->isDone() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!trace->isDone())
            ++ignored;
        // ~OpenMTRNetWrapper stops and joins, so a lost stop cannot hang here.
    }
    std::printf("stops ignored: %d of 10\n", ignored);
    CHECK(ignored == 0);
    return 0;
}

int main(int argc, char** argv)
{
    const std::string name = argc > 1 ? argv[1] : "";
    int rc = 0;                    // 77 = skipped (see pingSocketAvailable)
    if (name == "dns_lifetime")
        dns_lifetime();
    else if (name == "status_text")
        status_text();
    else if (name == "icmp_checksum")
        icmp_checksum();
    else if (name == "name_control_chars")
        name_control_chars();
    else if (name == "no_thread_for_dns")
        rc = no_thread_for_dns();
    else if (name == "v6_silent_first_hop")
        rc = v6_silent_first_hop();
    else if (name == "stop_before_start")
        rc = stop_before_start();
    else if (name == "stop_right_after_start")
        rc = stop_right_after_start();
    else {
        std::printf("unknown case \"%s\"\n", name.c_str());
        return 2;
    }
    if (g_failures)
        return 1;
    return rc;
}
