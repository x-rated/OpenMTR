// ==========================================================================
//  cli.cpp — headless report mode (`OpenMTR --report ...`)
//
//  The window's trace without the window: resolve the target, wait for the
//  route to settle using the exact same conditions as MainWindow's warm-up
//  (see report_core.h — that is the whole point of sharing the code, not
//  reimplementing it), count for the requested duration, then print the
//  same report Copy/Export would produce (text, JSON or CSV) to stdout and
//  exit.
//
//  Deliberately does not construct a QApplication/QCoreApplication: only
//  QtCore value types (QString, QJsonDocument, QDateTime) are used, which
//  work standalone. No window, no event loop, no display needed.
//
//  Exit codes:
//    0    report printed, the destination replied
//    1    report printed, the destination never replied during the run
//    2    invalid command line
//    3    the target could not be resolved
//    4    the trace could not start (no ICMP socket/handle)
//    130  interrupted (Ctrl+C); the partial report is still printed
// ==========================================================================

#include "cli.h"
#include "report_core.h"
#include "asncache.h"
#include "tracer.h"
#include "updatecheck.h"
#include "versioninfo.h"

#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QIODevice>

// tracer.h (included above) already pulls in winsock2/ws2tcpip/windows.h on
// Windows and netdb.h/arpa/inet.h/unistd.h on POSIX — everything
// getaddrinfo(), SetConsoleCtrlHandler(), SetConsoleOutputCP() and isatty()
// below need. Only _isatty/_fileno live in <io.h>, which tracer.h has no
// reason to pull in itself.
#ifdef _WIN32
#include <io.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <stop_token>
#include <thread>

namespace {

enum ExitCode {
    ExitOk          = 0,
    ExitNoReply     = 1,
    ExitUsage       = 2,
    ExitResolve     = 3,
    ExitEngine      = 4,
    ExitOutputError = 5,
    ExitInterrupted = 130,
};

enum class OutputFormat { Text, Json, Csv };

// Set for the duration of run() so the OS signal/console handler (which
// cannot capture any context) has somewhere to report Ctrl+C.
std::atomic<bool>* g_interruptedFlag = nullptr;
std::stop_source*  g_stopSource      = nullptr;

#ifdef _WIN32
BOOL WINAPI consoleCtrlHandler(DWORD signalType)
{
    if (signalType == CTRL_C_EVENT || signalType == CTRL_BREAK_EVENT) {
        if (g_interruptedFlag) g_interruptedFlag->store(true);
        if (g_stopSource)      g_stopSource->request_stop();
        return TRUE;
    }
    return FALSE;
}
#else
void sigintHandler(int)
{
    if (g_interruptedFlag) g_interruptedFlag->store(true);
    if (g_stopSource)      g_stopSource->request_stop();
}
#endif

// Writes text that may contain non-ASCII characters (the "·" in --version,
// a hostname from a reverse lookup) to a stdio stream that may be a real
// Windows console, or may have been redirected to a file or a pipe -
// rendering correctly either way.
//
// The first approach here was SetConsoleOutputCP(CP_UTF8) around the whole
// run, on the theory that the console's output code page (852 on a Czech
// system, turning "·" - bytes C2 B7 - into "┬Ě") just needed switching to
// UTF-8 for the duration. It did not reliably work: a console's code page is
// separate from its font, older conhost's raster ("Terminal") font cannot
// render code page 65001 at all, and buffered CRT stdio (printf/fputs) going
// through it does not consistently pick up the change either - the same
// "┬Ě" kept appearing. Switching a shared, outlived-by-us piece of console
// state and having to restore it afterwards was also more machinery than the
// problem needed.
//
// The reliable fix sidesteps code pages entirely: when the destination is an
// actual console, convert to UTF-16 and call WriteConsoleW, which Windows
// always renders correctly regardless of the console's code page or font.
// When the destination is not a console (redirected to a file, piped into
// another program), there is no code page or font to fight, and the other
// end expects UTF-8 bytes - exactly what this always wrote and what the
// non-Windows branch below still writes.
#ifdef _WIN32
void writeText(FILE* stream, const QString& text)
{
    const HANDLE handle = GetStdHandle(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD consoleMode = 0;
    if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &consoleMode)) {
        const std::wstring utf16 = text.toStdWString();
        DWORD written = 0;
        WriteConsoleW(handle, utf16.c_str(), static_cast<DWORD>(utf16.size()), &written, nullptr);
        return;
    }
    std::fputs(text.toUtf8().constData(), stream);
}
#else
void writeText(FILE* stream, const QString& text)
{
    std::fputs(text.toUtf8().constData(), stream);
}
#endif

// True when stderr is an actual terminal a human is watching, as opposed to
// redirected to a file/pipe (`--report ... > report.txt`, a cron/Task
// Scheduler log, `| findstr ...`). Progress updates only make sense in the
// first case — in the second they would just be junk bytes sitting in a log
// file forever, since nothing ever prints a newline after a \r update.
bool stderrIsInteractive()
{
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

// Prints a single self-overwriting status line to stderr (never stdout —
// stdout is reserved for the report itself, so piping/redirecting it must
// never see progress noise mixed in). No-op when stderr isn't a terminal.
class Progress {
public:
    explicit Progress(bool enabled) : m_enabled(enabled) {}

    void update(const QString& text)
    {
        if (!m_enabled || m_finished) return;
        // Pad over whatever was on the line before (a shorter message
        // following a longer one would otherwise leave a stale tail).
        QString line = text;
        if (line.size() < m_lastLen) line += QString(m_lastLen - line.size(), QLatin1Char(' '));
        m_lastLen = text.size();
        const QString out = QStringLiteral("\r") + line;
        std::fputs(out.toUtf8().constData(), stderr);
        std::fflush(stderr);
    }

    // Clears the line so whatever prints next (the report, on stdout, or
    // just the shell prompt) doesn't end up sharing it — e.g. "Finishing..."
    // followed directly by "OpenMTR Export" with no line break between them.
    // Idempotent and called explicitly right before the report is printed;
    // the destructor is just a safety net for any path that returns early.
    void finish()
    {
        if (!m_enabled || m_finished) return;
        m_finished = true;
        const QString out = QStringLiteral("\r") + QString(m_lastLen, QLatin1Char(' ')) + QStringLiteral("\r");
        std::fputs(out.toUtf8().constData(), stderr);
        std::fflush(stderr);
    }

    ~Progress() { finish(); }

private:
    bool m_enabled;
    bool m_finished = false;
    int  m_lastLen = 0;
};

void printUsage(FILE* out)
{
    std::fprintf(out,
        "OpenMTR --report --time <sec> [options] <target>\n"
        "  --time <sec>        measure duration (required)\n"
        "  --size <bytes>      payload, 64-8192\n"
        "  -4 | -6             force IP version\n"
        "  --txt|--json|--csv  format (default: txt)\n"
        "  --output <file>     write to file\n"
        "  -v                  version\n");
}

struct CliOptions {
    QString      target;
    int          timeSeconds = 0;
    unsigned     pingSize    = 64;         // same default as the window's spin box
    int          wantFamily  = AF_INET6;   // same default as the window's IPv6 checkbox
    OutputFormat format      = OutputFormat::Text;
    QString      outputPath;               // empty = stdout
};

// Returns false (with an error already printed to stderr) on a usage error.
bool parseArgs(int argc, char** argv, CliOptions& opts, bool& helpRequested)
{
    helpRequested = false;
    bool haveTime   = false;
    bool haveFormat = false;

    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);

        if (a == QLatin1String("--report")) {
            continue;   // mode selector; main() already acted on it
        } else if (a == QLatin1String("-h") || a == QLatin1String("--h") || a == QLatin1String("--help")) {
            helpRequested = true;
            return true;
        } else if (a == QLatin1String("--time")) {
            if (++i >= argc) { std::fprintf(stderr, "--time needs a value\n"); return false; }
            bool ok = false;
            const int v = QString::fromLocal8Bit(argv[i]).toInt(&ok);
            if (!ok || v <= 0) { std::fprintf(stderr, "--time must be a positive number of seconds\n"); return false; }
            opts.timeSeconds = v;
            haveTime = true;
        } else if (a == QLatin1String("--size")) {
            if (++i >= argc) { std::fprintf(stderr, "--size needs a value\n"); return false; }
            bool ok = false;
            const int v = QString::fromLocal8Bit(argv[i]).toInt(&ok);
            // Same range as the window's ping-size spin box.
            if (!ok || v < 64 || v > 8192) { std::fprintf(stderr, "--size must be between 64 and 8192\n"); return false; }
            opts.pingSize = static_cast<unsigned>(v);
        } else if (a == QLatin1String("--output")) {
            if (++i >= argc) { std::fprintf(stderr, "--output needs a path\n"); return false; }
            opts.outputPath = QString::fromLocal8Bit(argv[i]);
        } else if (a == QLatin1String("-4")) {
            opts.wantFamily = AF_INET;
        } else if (a == QLatin1String("-6")) {
            opts.wantFamily = AF_INET6;
        } else if (a == QLatin1String("--json") || a == QLatin1String("--csv") || a == QLatin1String("--txt")) {
            if (haveFormat) { std::fprintf(stderr, "--txt, --json and --csv are mutually exclusive\n"); return false; }
            opts.format = (a == QLatin1String("--json")) ? OutputFormat::Json
                        : (a == QLatin1String("--csv"))  ? OutputFormat::Csv
                                                          : OutputFormat::Text;
            haveFormat = true;
        } else if (a.startsWith(QLatin1Char('-'))) {
            std::fprintf(stderr, "Unknown option: %s\n", qUtf8Printable(a));
            return false;
        } else {
            if (!opts.target.isEmpty()) { std::fprintf(stderr, "Only one target may be given\n"); return false; }
            opts.target = a;
        }
    }

    if (!haveTime)            { std::fprintf(stderr, "--time <seconds> is required\n"); return false; }
    if (opts.target.isEmpty()) { std::fprintf(stderr, "A target is required\n"); return false; }
    return true;
}

class CliOptionsProvider : public IOpenMTROptionsProvider {
public:
    explicit CliOptionsProvider(unsigned size) : m_size(size) {}
    [[nodiscard]] unsigned getPingSize() const noexcept override { return m_size; }
private:
    unsigned m_size;
};

// Resolves `target`, preferring `wantFamily` but falling back to whatever
// address family is available — identical to MainWindow::onStartStop()'s
// Start path, so a target that behaves one way in the GUI behaves the same
// way here.
bool resolveTarget(const QString& target, int wantFamily, SOCKADDR_INET& addr, bool& ipv6)
{
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    ipv6 = (wantFamily == AF_INET6);
    bool resolved = false;

    if (getaddrinfo(target.toStdString().c_str(), nullptr, &hints, &res) == 0 && res) {
        addrinfo* match = nullptr;
        for (addrinfo* r = res; r; r = r->ai_next)
            if (r->ai_family == wantFamily) { match = r; break; }
        if (!match)
            for (addrinfo* r = res; r; r = r->ai_next)
                if (r->ai_family == AF_INET || r->ai_family == AF_INET6) { match = r; break; }
        if (match) {
            std::memcpy(&addr, match->ai_addr,
                match->ai_addrlen < sizeof(addr) ? match->ai_addrlen : sizeof(addr));
            ipv6 = (match->ai_family == AF_INET6);
            resolved = true;
        }
        freeaddrinfo(res);
    }
    return resolved;
}

}  // namespace

int cli::printVersion()
{
    writeText(stdout, openMtrVersionLine() + QLatin1Char('\n'));
    return ExitOk;
}

int cli::run(int argc, char** argv)
{
    // See the comment on g_openMtrHeadless in tracer.h: this must be set
    // before DoTrace() can possibly be called below.
    g_openMtrHeadless.store(true);

    CliOptions opts;
    bool helpRequested = false;
    if (!parseArgs(argc, argv, opts, helpRequested)) {
        printUsage(stderr);
        return ExitUsage;
    }
    if (helpRequested) {
        printUsage(stdout);
        return ExitOk;
    }

    SOCKADDR_INET addr = {};
    bool ipv6 = false;
    if (!resolveTarget(opts.target, opts.wantFamily, addr, ipv6)) {
        std::fprintf(stderr, "Could not resolve \"%s\".\n", qUtf8Printable(opts.target));
        return ExitResolve;
    }

#if OPENMTR_ENABLE_UPDATE_CHECK
    // Same check, same blocking function, same GitHub endpoint as the GUI's
    // toolbar badge (see checkForUpdates() in MainWindow.cpp and
    // updatecheck.h/.cpp) — run here on a background thread so it can never
    // delay the trace itself, with its result printed to stderr whenever it
    // arrives. Left commented out of the CLI's behaviour entirely (like the
    // GUI) by OPENMTR_ENABLE_UPDATE_CHECK defaulting to 0 in CMakeLists.txt.
    std::thread([]() {
        const UpdateInfo info = checkForUpdateBlocking();
        if (info.available)
            std::fprintf(stderr, "\nA newer OpenMTR is available: %s\n%s\n\n",
                         qUtf8Printable(info.version), qUtf8Printable(info.url));
    }).detach();
#endif // OPENMTR_ENABLE_UPDATE_CHECK

    std::atomic<bool> interrupted{false};
    std::stop_source  stopSource;
    g_interruptedFlag = &interrupted;
    g_stopSource      = &stopSource;
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, sigintHandler);
#endif

    CliOptionsProvider  provider(opts.pingSize);
    OpenMTRNetWrapper    net(&provider);
    if (net.DoTrace(stopSource.get_token(), addr) != 0) {
        // DoTrace() already reported the specific failure itself (see
        // OpenMTRNetWrapper::DoTrace in tracer.h).
        return ExitEngine;
    }

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto elapsedMs = [&]() -> qint64 {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    };

    AsnCache asnCache;
    const auto asnLookup = [&](const QString& ip, bool v6) { return asnCache.get(ip, v6); };
    Progress progress(stderrIsInteractive());

    // --- Phase 1: route discovery / warm-up ---------------------------
    // Same conditions as MainWindow::onWarmupEnd(), just driven by a
    // blocking sleep instead of QTimer::singleShot chaining.
    WarmupFingerprint fp;
    for (;;) {
        if (interrupted.load()) break;
        const int maxHops   = net.GetMax();
        const auto state    = net.getCurrentState();
        const int checkHops = std::min(maxHops, static_cast<int>(state.size()));
        // Warm the ASN cache as addresses appear, same as the GUI, so
        // lookups run concurrently with route discovery.
        for (const auto& h : state)
            if (h.addr.Ipv4.sin_family != AF_UNSPEC)
                asnCache.get(QString::fromStdWString(addr_to_wstring(h.addr)),
                             h.addr.Ipv6.sin6_family == AF_INET6);
        progress.update(QString("Discovering route... (%1s)").arg(elapsedMs() / 1000));
        if (warmupRouteSettled(fp, state, checkHops, elapsedMs()))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (!interrupted.load()) {
        // Same fixed settle pause the GUI gives itself before polling
        // ASN/reverse-DNS (see onWarmupEnd()'s QTimer::singleShot(400, ...)).
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        DnsSettle dns;
        for (;;) {
            if (interrupted.load()) break;
            const auto state = net.getCurrentState();
            progress.update(QStringLiteral("Resolving hostnames/ASN..."));
            if (dnsAndAsnSettled(dns, state, asnCache.hasPending(), elapsedMs()))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }

    // --- Phase 2: counting window ---------------------------------------
    QDateTime testStarted   = QDateTime::currentDateTime();
    qint64 countingStartMs  = elapsedMs();
    if (!interrupted.load()) {
        net.resetStats();
        testStarted     = QDateTime::currentDateTime();
        countingStartMs = elapsedMs();
        const qint64 deadlineMs = countingStartMs + static_cast<qint64>(opts.timeSeconds) * 1000;
        while (!interrupted.load() && elapsedMs() < deadlineMs) {
            const qint64 remaining = (deadlineMs - elapsedMs() + 999) / 1000;   // round up
            progress.update(QString("Measuring... %1s remaining").arg(std::max<qint64>(remaining, 0)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    // Duration is measured against the counting window itself, ending here —
    // not against however long the engine then takes below to gracefully
    // wind down probes already in flight (one can take up to its own
    // timeout to notice the stop request). Computing it after that wait
    // instead would silently inflate the reported duration past what was
    // actually asked for with --time.
    qint64 durationMs = elapsedMs() - countingStartMs;
    if (durationMs < 0) durationMs = 0;

    progress.update(QStringLiteral("Finishing..."));
    stopSource.request_stop();
    while (!net.isDone())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto finalState = net.getCurrentState();
    const auto rows       = computeReportRows(finalState, asnLookup);

    QString report;
    switch (opts.format) {
        case OutputFormat::Json: report = buildJsonReport(opts.target, testStarted, durationMs, rows, finalState); break;
        case OutputFormat::Csv:  report = buildCsvReport(rows); break;
        case OutputFormat::Text: report = buildTextReport(opts.target, testStarted, durationMs, rows, finalState, ipv6); break;
    }

    // Clear the progress line before writing to stdout — otherwise, on a
    // real terminal, the report's first line would visually run on right
    // after "Finishing..." on the same row (stdout and stderr share the
    // screen but not a cursor position, so nothing else would separate
    // them).
    progress.finish();
    if (!opts.outputPath.isEmpty()) {
        // Written directly by us, independent of the shell: works with no
        // shell or console at all (Task Scheduler, a service) and without
        // depending on how a given shell implements stdout redirection.
        QFile f(opts.outputPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            std::fprintf(stderr, "Could not write \"%s\": %s\n",
                         qUtf8Printable(opts.outputPath), qUtf8Printable(f.errorString()));
            return ExitOutputError;
        }
        const qint64 bytesWritten = f.write(report.toUtf8());
        f.close();
        // Otherwise there is no visible sign the run ever finished: no
        // stdout output at all in this branch, and nothing was printed
        // since the last progress update was cleared.
        std::fprintf(stderr, "Report written to %s (%lld bytes).\n",
                     qUtf8Printable(opts.outputPath), static_cast<long long>(bytesWritten));
    } else {
        QString out = report;
        if (!out.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
        writeText(stdout, out);
        std::fflush(stdout);
    }

    if (interrupted.load())
        return ExitInterrupted;

    // Reached: the resolved destination address appeared among the hops
    // that actually answered at least once during the run.
    const QString destStr = QString::fromStdWString(addr_to_wstring(addr));
    bool reached = false;
    for (const auto& h : finalState) {
        if (h.addr.Ipv4.sin_family == AF_UNSPEC) continue;
        if (QString::fromStdWString(addr_to_wstring(h.addr)) == destStr) {
            reached = true;
            break;
        }
    }
    return reached ? ExitOk : ExitNoReply;
}
