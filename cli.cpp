// ==========================================================================
//  cli.cpp — headless report mode (OpenMTR --report ...)
//
//  The window's trace without the window: resolve the target, let the route
//  settle, count a fixed number of probe cycles, then print the same report
//  Copy/Export produce (text or JSON) to stdout and exit. Only QtCore is
//  used — no QApplication, so it runs over SSH, from cron or in CI with no
//  display at all.
//
//  Exit codes (also listed in --help):
//    0    report printed, the destination replied
//    1    report printed, the destination never replied during the run
//    2    invalid command line
//    3    the target could not be resolved
//    4    the trace could not start (no ICMP socket / handle)
//    130  interrupted (Ctrl+C); the partial report is still printed
// ==========================================================================

#include "cli.h"
#include "report.h"
#include "version.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

#ifdef _WIN32
#include <io.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>
#include <thread>
#include <vector>

namespace {

// ==========================================================================
//  Constants
// ==========================================================================

enum ExitCode {
    ExitOk          = 0,
    ExitNoReply     = 1,
    ExitUsage       = 2,
    ExitResolve     = 3,
    ExitEngine      = 4,
    ExitInterrupted = 130,
};

constexpr int    kDefaultCount = 10;
constexpr int    kMaxCount     = 100000;
constexpr double kMinInterval  = 0.1;
constexpr double kMaxInterval  = 60.0;
// Same range as the window's ping size box.
constexpr int    kMinSize      = 64;
constexpr int    kMaxSize      = 8192;

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

// How often the run loops below look at the engine.
constexpr Ms kPollStep{100};
// Warm-up: like the window, statistics start only once the discovered route
// holds steady, so the report does not mix in discovery probes. The window
// waits for more (every silent hop to time out twice, so no row pops in
// after the reveal); a report is printed only at the end, so it needs just
// a stable route. Probes to the 30 TTLs start 50 ms apart, so the route
// cannot settle in less than about 1.5 s. The deadline is the window's.
constexpr Ms kWarmupMinStable{2000};
constexpr Ms kWarmupDeadline{12000};
// After counting: time given to reverse-DNS names and ASN lookups still in
// flight. A name that has not come by then is left as the bare address.
constexpr Ms kNameGrace{3000};
constexpr Ms kNameStall{1000};

// ==========================================================================
//  Output
// ==========================================================================

void writeTo(FILE* f, const QString& text)
{
#ifdef _WIN32
    // A console gets UTF-16 through WriteConsoleW: every character shows
    // whatever the console's code page is, and that code page — shared with
    // the shell, and outliving us — is left alone. Files and pipes get UTF-8.
    // Tried on any character device rather than gated on GetConsoleMode(),
    // which needs read access a write-only console handle lacks; NUL and COM
    // ports refuse WriteConsoleW and fall through to the byte path.
    std::fflush(f);
    const int fd = _fileno(f);            // negative when the stream has no handle
    if (fd >= 0) {
        const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_CHAR) {
            const auto* p = reinterpret_cast<const wchar_t*>(text.utf16());
            DWORD left = static_cast<DWORD>(text.size());
            bool  wrote = false;
            while (left > 0) {
                DWORD written = 0;
                if (!WriteConsoleW(h, p, left, &written, nullptr) || written == 0)
                    break;
                wrote = true;
                p    += written;
                left -= written;
            }
            if (wrote || left == 0)
                return;
        }
    }
#endif
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), f);
    std::fflush(f);
}

// The name the user knows this program by: "openmtr-cli" for the
// standalone binary, "OpenMTR" for the window app's --report mode.
QString g_prog = QStringLiteral("OpenMTR");

void printError(const QString& text)
{
    writeTo(stderr, QStringLiteral("%1: %2\n").arg(g_prog, text));
}

#ifdef _WIN32
// True when a std stream has no destination of its own and should go to the
// console: no handle at all, or a console handle. NUL and COM ports are
// character devices too, but GetConsoleMode() fails on them, so `>nul` stays
// silent. Only meaningful once we are attached to the console.
bool wantsConsole(HANDLE h)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return true;
    switch (GetFileType(h)) {
    case FILE_TYPE_DISK:
    case FILE_TYPE_PIPE:
        return false;
    case FILE_TYPE_CHAR: {
        DWORD mode = 0;
        return GetConsoleMode(h, &mode) != 0;
    }
    default:
        return true;
    }
}

// OpenMTR.exe is a GUI-subsystem program, so Windows gives it no console of
// its own. Attach to the console of the shell that started us — always, even
// when both streams are redirected, because Ctrl+C only reaches processes
// attached to the console — and send every stream the shell did not
// redirect there.
void attachParentConsole()
{
    // Read before attaching: attaching may fill in std handles that were
    // unset.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;                       // started without a console (Explorer, a service)
    // "w+": read access too, which GetConsoleMode() needs on these handles.
    FILE* stream = nullptr;
    if (wantsConsole(out))
        freopen_s(&stream, "CONOUT$", "w+", stdout);
    if (wantsConsole(err))
        freopen_s(&stream, "CONOUT$", "w+", stderr);
}
#endif

// Qt's own diagnostics (e.g. a QProcess warning) would land in a script's
// stderr next to our messages; keep only the ones that mean something broke.
void quietQtMessages(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (type == QtCriticalMsg || type == QtFatalMsg)
        writeTo(stderr, msg + QLatin1Char('\n'));
}

// ==========================================================================
//  Interruption
// ==========================================================================

std::atomic<bool> g_interrupted{false};

// First Ctrl+C (or SIGTERM): finish early and still print what was
// measured. The default action is restored, so a second one ends the
// process at once.
extern "C" void onInterrupt(int sig)
{
    g_interrupted.store(true);
    std::signal(sig, SIG_DFL);
}

// Catch `sig` unless the parent made us ignore it: a script's `OpenMTR -r
// ... &` starts with SIGINT ignored, and a Ctrl+C meant for the script's
// foreground command must not cut the background report short.
void catchSignal(int sig)
{
    if (std::signal(sig, onInterrupt) == SIG_IGN)
        std::signal(sig, SIG_IGN);
}

// ==========================================================================
//  Target & engine helpers
// ==========================================================================

// Resolve `target`. `family` is AF_INET or AF_INET6 when -4/-6 forced one,
// else AF_UNSPEC: IPv4 preferred, IPv6 as the fallback, as in the window.
bool resolveTarget(const QString& target, int family, SOCKADDR_INET& out)
{
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = family;
    hints.ai_socktype = SOCK_DGRAM;   // one entry per address, not per socket type
    if (getaddrinfo(target.toStdString().c_str(), nullptr, &hints, &res) != 0 || !res)
        return false;
    auto resGuard = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>(res, freeaddrinfo);

    const addrinfo* match = nullptr;
    for (const int want : {AF_INET, AF_INET6})
        for (const addrinfo* r = res; r && !match; r = r->ai_next)
            if (r->ai_family == want) match = r;
    if (!match)
        return false;
    out = {};
    std::memcpy(&out, match->ai_addr,
                match->ai_addrlen < sizeof(out) ? match->ai_addrlen : sizeof(out));
    return true;
}

bool isV6(const SOCKADDR_INET& a)  { return a.Ipv6.sin6_family == AF_INET6; }
// Whether the hop has answered: it has a real address. Not just a family —
// the Windows engine sets hop 1's family before any probe goes out
// (tracer.cpp), leaving 0.0.0.0 / :: there until a reply arrives.
bool hasAddr(const SOCKADDR_INET& a)
{
    if (a.Ipv4.sin_family == AF_INET)
        return a.Ipv4.sin_addr.s_addr != 0;
    if (a.Ipv6.sin6_family == AF_INET6) {
        static const in6_addr zero{};
        return std::memcmp(&a.Ipv6.sin6_addr, &zero, sizeof(zero)) != 0;
    }
    return false;
}

bool sameAddress(const SOCKADDR_INET& a, const SOCKADDR_INET& b)
{
    if (a.Ipv4.sin_family != b.Ipv4.sin_family)
        return false;
    if (a.Ipv4.sin_family == AF_INET)
        return std::memcmp(&a.Ipv4.sin_addr, &b.Ipv4.sin_addr, sizeof(a.Ipv4.sin_addr)) == 0;
    if (a.Ipv6.sin6_family == AF_INET6)
        return std::memcmp(&a.Ipv6.sin6_addr, &b.Ipv6.sin6_addr, sizeof(a.Ipv6.sin6_addr)) == 0;
    return false;
}

QString addressText(const SOCKADDR_INET& a)
{
    return QString::fromStdWString(addr_to_wstring(a));
}

// The route as the window's warm-up sees it: hop count plus every hop's
// address. Any change restarts the stability window.
QByteArray routeFingerprint(const std::vector<OpenMTRHostInfo>& state)
{
    QByteArray fp;
    fp.append(static_cast<char>(state.size()));
    for (const auto& h : state) {
        if (h.addr.Ipv4.sin_family == AF_INET)
            fp.append(reinterpret_cast<const char*>(&h.addr.Ipv4.sin_addr), sizeof(h.addr.Ipv4.sin_addr));
        else if (h.addr.Ipv6.sin6_family == AF_INET6)
            fp.append(reinterpret_cast<const char*>(&h.addr.Ipv6.sin6_addr), sizeof(h.addr.Ipv6.sin6_addr));
        else
            fp.append('\0');
    }
    return fp;
}

// ASN lookups, one detached thread per address as it appears (the window
// does the same), so they run while probes are counted. Shared with the
// threads, which may outlive a run cut short.
struct AsnCache {
    std::mutex                 mutex;
    std::map<QString, QString> results;   // address -> AS number ("" = none)
    std::set<QString>          pending;

    void request(const QString& ip, bool v6, const std::shared_ptr<AsnCache>& self)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (results.count(ip) || !pending.insert(ip).second)
                return;
        }
        std::thread([self, ip, v6] {
            const QString asn = lookupAsn(ip, v6);
            std::lock_guard<std::mutex> lock(self->mutex);
            self->pending.erase(ip);
            self->results[ip] = asn;
        }).detach();
    }
    QString get(const QString& ip)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = results.find(ip);
        return it == results.end() ? QString() : it->second;
    }
    bool busy()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !pending.empty();
    }
};

} // namespace

// ==========================================================================
//  Entry points
// ==========================================================================

namespace {

// "-r", "-rn", "-rjc5", "-hv": a cluster of report mode's short options
// (runReportMode() defines them), read the way QCommandLineParser reads it —
// flags r n j 4 6 h ? v, while c/i/s take the rest of the cluster as their
// value. Any other cluster is not ours (Qt's own GUI options such as
// -platform, -visual), and the window gets it — except one that starts with
// r: Qt's only such option is -reverse, so anything else there is report
// mode with a typo or an mtr-only letter (-rwc 10), which the parser then
// reports as a usage error instead of the window opening.
bool isReportCluster(const char* a)
{
    if (a[0] != '-' || a[1] == '\0' || a[1] == '-')
        return false;
    if (a[1] == 'r')
        return std::strcmp(a, "-reverse") != 0;
    bool report = false, sawR = false;
    for (const char* p = a + 1; *p; ++p) {
        if (*p == 'r')
            report = sawR = true;
        else if (std::strchr("hv?", *p))
            report = true;
        else if (std::strchr("cis", *p))
            return sawR;                  // the rest is a value; -c/-i/-s need -r (not -visual)
        else if (!std::strchr("nj46", *p))
            return false;
    }
    return report;
}

} // namespace

bool isReportModeRequested(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--report") || !std::strcmp(a, "--help")
            || !std::strcmp(a, "--version") || isReportCluster(a))
            return true;
    }
    return false;
}

int runReportMode(int argc, char* argv[], bool standalone)
{
    g_prog = standalone ? QStringLiteral("openmtr-cli") : QStringLiteral("OpenMTR");
#ifdef _WIN32
    // openmtr-cli.exe is a console program and has its console already.
    if (!standalone)
        attachParentConsole();
#endif
    qInstallMessageHandler(quietQtMessages);

    QCoreApplication app(argc, argv);
    app.setApplicationName("OpenMTR");
    app.setApplicationVersion(OPENMTR_VERSION);

    // ---- Command line ------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(QString(standalone
        ? "openmtr-cli: trace the route to <target> like OpenMTR does, for a fixed\n"
          "number of cycles, then print the report to stdout and exit.\n"
        : "OpenMTR report mode: trace the route to <target> for a fixed number of\n"
          "cycles, print the report to stdout and exit, without opening a window.\n") +
        "\n"
        "Exit codes:\n"
        "  0    report printed, the destination replied\n"
        "  1    report printed, the destination never replied during the run\n"
        "  2    invalid command line\n"
        "  3    the target could not be resolved\n"
        "  4    the trace could not start (no ICMP socket)\n"
        "  130  interrupted with Ctrl+C (the partial report is still printed)");
    // openmtr-cli always reports; it still accepts -r so `mtr -r`-style
    // command lines work unchanged.
    const QCommandLineOption reportOpt({"r", "report"}, standalone
        ? QStringLiteral("Accepted for compatibility with mtr; always on.")
        : QStringLiteral("Run without a window and print a report (required)."));
    const QCommandLineOption countOpt({"c", "count"},
        QString("Probes to send to each hop that replies before reporting (default %1).").arg(kDefaultCount), "N");
    const QCommandLineOption intervalOpt({"i", "interval"},
        QString("Seconds between probes to each hop, %1 to %2 (default 1).").arg(kMinInterval).arg(kMaxInterval),
        "seconds");
    const QCommandLineOption sizeOpt({"s", "size"},
        QString("ICMP payload size in bytes, %1 to %2 (default %1).").arg(kMinSize).arg(kMaxSize), "bytes");
    const QCommandLineOption v4Opt("4", "Use IPv4 only.");
    const QCommandLineOption v6Opt("6", "Use IPv6 only.");
    const QCommandLineOption noDnsOpt({"n", "no-dns"}, "Do not resolve host names of hops.");
    const QCommandLineOption noAsnOpt("no-asn", "Do not look up AS numbers.");
    const QCommandLineOption jsonOpt({"j", "json"}, "Print the report as JSON.");
    const QCommandLineOption helpOpt({"h", "?", "help"}, "Show this help and exit.");
    const QCommandLineOption versionOpt({"v", "version"}, "Show the version and exit.");
    for (const auto* o : {&reportOpt, &countOpt, &intervalOpt, &sizeOpt, &v4Opt, &v6Opt,
                          &noDnsOpt, &noAsnOpt, &jsonOpt, &helpOpt, &versionOpt})
        parser.addOption(*o);
    parser.addPositionalArgument("target", "Host name or IP address to trace.");

    // parse(), not process(): process() would print (or, in a Windows GUI
    // program, show a message box) and exit on its own terms.
    if (!parser.parse(app.arguments())) {
        printError(parser.errorText() + QString("\nTry '%1 --help'.").arg(g_prog));
        return ExitUsage;
    }
    if (parser.isSet(helpOpt)) {
        QString help = parser.helpText();
        // Run from the AppImage, argv[0] is its temporary mount
        // (/tmp/.mount_*/usr/bin/OpenMTR), gone once we exit; show the path
        // the user ran instead, which the AppImage runtime puts in ARGV0.
        // Only when we really run from it: other AppImages leak these
        // variables into every process they start.
        const QString shown  = qEnvironmentVariable("ARGV0");
        const QString appDir = qEnvironmentVariable("APPDIR");
        const QString argv0  = app.arguments().constFirst();
        const QString prefix = QStringLiteral("Usage: ") + argv0;
        if (!appDir.isEmpty() && argv0.startsWith(appDir + QLatin1Char('/'))
            && !shown.isEmpty() && help.startsWith(prefix))
            help.replace(0, prefix.size(), QStringLiteral("Usage: ") + shown);
        writeTo(stdout, help);
        return ExitOk;
    }
    if (parser.isSet(versionOpt)) {
        writeTo(stdout, QStringLiteral("%1 %2\n").arg(g_prog, QStringLiteral(OPENMTR_VERSION)));
        return ExitOk;
    }

    auto usageError = [](const QString& text) {
        printError(text + QString("\nTry '%1 --help'.").arg(g_prog));
        return ExitUsage;
    };
    if (!standalone && !parser.isSet(reportOpt))
        return usageError("--report is required to run without a window.");
    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1)
        return usageError(positional.isEmpty() ? QStringLiteral("No target given.")
                                               : QStringLiteral("Only one target can be given."));
    const QString target = positional.first();

    int count = kDefaultCount;
    if (parser.isSet(countOpt)) {
        bool ok = false;
        count = parser.value(countOpt).toInt(&ok);
        if (!ok || count < 1 || count > kMaxCount)
            return usageError(QString("--count must be a whole number from 1 to %1.").arg(kMaxCount));
    }
    double interval = 1.0;
    if (parser.isSet(intervalOpt)) {
        bool ok = false;
        interval = parser.value(intervalOpt).toDouble(&ok);
        if (!ok || !(interval >= kMinInterval && interval <= kMaxInterval))
            return usageError(QString("--interval must be from %1 to %2 seconds.").arg(kMinInterval).arg(kMaxInterval));
    }
    int size = kMinSize;
    if (parser.isSet(sizeOpt)) {
        bool ok = false;
        size = parser.value(sizeOpt).toInt(&ok);
        if (!ok || size < kMinSize || size > kMaxSize)
            return usageError(QString("--size must be from %1 to %2 bytes.").arg(kMinSize).arg(kMaxSize));
    }
    if (parser.isSet(v4Opt) && parser.isSet(v6Opt))
        return usageError("-4 and -6 cannot be used together.");
    const int family = parser.isSet(v4Opt) ? AF_INET : parser.isSet(v6Opt) ? AF_INET6 : AF_UNSPEC;
    const bool useDns = !parser.isSet(noDnsOpt);
    const bool useAsn = !parser.isSet(noAsnOpt);
    const bool json   = parser.isSet(jsonOpt);

    // ---- Target ------------------------------------------------------------
    SOCKADDR_INET dest = {};
    if (!resolveTarget(target, family, dest)) {
        printError(family == AF_INET  ? QString("Could not resolve \"%1\" to an IPv4 address.").arg(target)
                 : family == AF_INET6 ? QString("Could not resolve \"%1\" to an IPv6 address.").arg(target)
                                      : QString("Could not resolve \"%1\".").arg(target));
        return ExitResolve;
    }
    const bool v6 = isV6(dest);

    // ---- Trace -------------------------------------------------------------
    catchSignal(SIGINT);
#ifdef SIGTERM
    catchSignal(SIGTERM);
#endif

    // The engine does not say why it could not start; on POSIX, open a
    // socket like its own to find out, so the advice fits the cause.
    const auto engineError = [&] {
#ifndef _WIN32
        const int fd  = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM,
                                 v6 ? int(IPPROTO_ICMPV6) : int(IPPROTO_ICMP));
        const int err = fd < 0 ? errno : 0;
        if (fd >= 0)
            ::close(fd);
        if (err == EAFNOSUPPORT) {
            printError("Could not open an ICMP socket: IPv6 is not available on this system.");
            return ExitEngine;
        }
#ifdef __linux__
        if (err == EACCES || err == EPERM) {
            printError("Could not open an ICMP socket: unprivileged ping sockets are disabled. "
                       "Allow them with: sudo sysctl -w net.ipv4.ping_group_range=\"0 2147483647\"");
            return ExitEngine;
        }
#endif
        if (err != 0) {
            printError(QString("Could not open an ICMP socket: %1").arg(QString::fromLocal8Bit(std::strerror(err))));
            return ExitEngine;
        }
#endif
        printError("Could not open an ICMP socket.");
        return ExitEngine;
    };

    OpenMTROptions opts;
    opts.pingsize = static_cast<unsigned>(size);
    opts.interval = interval;
    opts.useDNS   = useDns;

    auto net = std::make_unique<OpenMTRNetWrapper>();
    std::stop_source stop;
    if (net->DoTrace(stop.get_token(), dest, opts) != 0)
        return engineError();

    auto asn = std::make_shared<AsnCache>();
    auto requestAsns = [&](const std::vector<OpenMTRHostInfo>& state) {
        if (!useAsn) return;
        for (const auto& h : state)
            if (hasAddr(h.addr))
                asn->request(addressText(h.addr), isV6(h.addr), asn);
    };

    // Warm-up: until the route holds steady (or the deadline passes).
    const auto traceStart = Clock::now();
    // Moved to the start of counting below; kept here should Ctrl+C end the
    // run during the warm-up.
    QDateTime started = QDateTime::currentDateTime();
    auto countStart   = traceStart;
    const Ms   minStable  = std::max(kWarmupMinStable, Ms(static_cast<long long>(interval * 1000) + 250));
    QByteArray fingerprint;
    auto lastChange = traceStart;
    while (!g_interrupted.load()) {
        std::this_thread::sleep_for(kPollStep);
        // The engine only ends on its own when it could not open its socket.
        if (net->isDone())
            return engineError();
        const auto state = net->getCurrentState();
        requestAsns(state);
        const QByteArray fp = routeFingerprint(state);
        const auto now = Clock::now();
        if (fp != fingerprint) {
            fingerprint = fp;
            lastChange  = now;
        }
        if (now - lastChange >= minStable || now - traceStart >= kWarmupDeadline)
            break;
    }

    // Counting: statistics restart now, so they describe this window only.
    // Done once every hop that has replied (at any time, warm-up included)
    // has been probed `count` times, every other hop has at least one
    // finished probe (so no row is left without a result), and some hop has
    // reached `count`. A lost probe only finishes when it times out (5 s), so
    // lossy hops, or a route where nothing replies, run to the deadline below
    // ((count + 1) periods + one timeout + 1 s) and may end with fewer
    // probes. Parking stays off: with it, a hop past the route edge could
    // miss the whole window.
    if (!g_interrupted.load()) {
        net->resetStats(false);
        started    = QDateTime::currentDateTime();
        countStart = Clock::now();
        // The engines' real period (tracer.cpp), plus one period because a
        // hop can be anywhere in its slot when the statistics reset.
        const long long periodMs = static_cast<long long>(interval * 1000) + PROBE_PERIOD_PAD_MS;
        const auto deadline = countStart
            + Ms((static_cast<long long>(count) + 1) * periodMs + ECHO_REPLY_TIMEOUT + 1000);
        while (!g_interrupted.load()) {
            std::this_thread::sleep_for(kPollStep);
            if (net->isDone())
                return engineError();
            const auto state = net->getCurrentState();
            requestAsns(state);
            int  maxSent = 0;
            bool done    = true;
            for (const auto& h : state) {
                maxSent = std::max(maxSent, h.xmit);
                if (h.xmit == 0 || (hasAddr(h.addr) && h.xmit < count))
                    done = false;
            }
            if ((done && maxSent >= count) || Clock::now() >= deadline)
                break;
        }
    }
    const qint64 durationMs =
        std::chrono::duration_cast<Ms>(Clock::now() - countStart).count();
    const bool interrupted = g_interrupted.load();

    // Stop probing. The engine object stays alive, so reverse-DNS answers
    // still in flight can land while we wait for them below.
    stop.request_stop();
    while (!net->isDone())
        std::this_thread::sleep_for(Ms(10));

    if (!interrupted) {
        const auto graceEnd = Clock::now() + kNameGrace;
        int  named = -1;
        auto lastProgress = Clock::now();
        while (Clock::now() < graceEnd && !g_interrupted.load()) {
            const auto state = net->getCurrentState();
            requestAsns(state);
            int addressed = 0, nowNamed = 0;
            for (const auto& h : state) {
                if (!hasAddr(h.addr)) continue;
                ++addressed;
                if (h.getName() != addr_to_wstring(h.addr)) ++nowNamed;
            }
            if (nowNamed > named) {
                named = nowNamed;
                lastProgress = Clock::now();
            }
            const bool namesSettled = !useDns || nowNamed >= addressed
                                   || Clock::now() - lastProgress >= kNameStall;
            if (namesSettled && !asn->busy())
                break;
            std::this_thread::sleep_for(kPollStep);
        }
    }

    // ---- Report ------------------------------------------------------------
    const auto state = net->getCurrentState();
    std::vector<ReportRow> rows;
    bool reached = false;
    for (int i = 0; i < static_cast<int>(state.size()); ++i) {
        const auto& h = state[i];
        rows.push_back(makeReportRow(i, h, hasAddr(h.addr) && useAsn ? asn->get(addressText(h.addr)) : QString()));
        // The engine records an address only from a reply, so this is "the
        // destination answered at some point", even if the counting window
        // happened to catch only its losses.
        if (hasAddr(h.addr) && sameAddress(h.addr, dest))
            reached = true;
    }

    ReportInfo info;
    info.title      = QStringLiteral("OpenMTR Report");
    info.target     = target;
    info.address    = addressText(dest);
    info.started    = started;
    info.durationMs = durationMs;
    info.ipv6       = v6;

    if (json) {
        QJsonObject extra;
        extra["address"]             = info.address;
        extra["ip_version"]          = v6 ? 6 : 4;
        extra["count"]               = count;
        extra["interval"]            = interval;
        extra["size"]                = size;
        extra["destination_reached"] = reached;
        extra["interrupted"]         = interrupted;
        writeTo(stdout, buildJsonReport(info, rows, extra));
    } else {
        writeTo(stdout, buildTextReport(info, rows));
    }
    if (interrupted)
        printError("Interrupted; the report covers only the probes sent so far.");

    const int rc = interrupted ? ExitInterrupted : reached ? ExitOk : ExitNoReply;

    // ASN lookups still running (a slow resolver, or a run cut short) hold
    // a reference to Qt; tearing QCoreApplication down under them is not
    // safe. Everything worth keeping has been written, so leave at once.
    if (asn->busy()) {
        std::fflush(nullptr);
        std::_Exit(rc);
    }
    return rc;
}
