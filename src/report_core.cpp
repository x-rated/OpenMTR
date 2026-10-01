#include "report_core.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

// ==========================================================================
//  Columns
// ==========================================================================

const QStringList COLUMNS = {
    "Hop", "ASN", "Hostname", "IP", "Loss %", "Sent", "Recv",
    "Best ms", "Avrg ms", "Wrst ms", "Last ms", "Jttr ms"
};

// ==========================================================================
//  Status text / duration formatting
// ==========================================================================

QString describeStatus(unsigned long status, bool ipv6)
{
    QString text = QString::fromLatin1(OpenMTRStatusText(status, ipv6));
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return QStringLiteral("%1, code %2").arg(text).arg(status);
}

QString formatDuration(qint64 ms)
{
    qint64 secs = ms / 1000;
    int h = static_cast<int>(secs / 3600), m = static_cast<int>((secs % 3600) / 60), s = static_cast<int>(secs % 60);
    return h > 0
        ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// ==========================================================================
//  Per-hop report row
// ==========================================================================

std::vector<ReportRow> computeReportRows(const std::vector<OpenMTRHostInfo>& state,
                                          const AsnLookupFn& asnLookup)
{
    std::vector<ReportRow> rows;
    rows.reserve(state.size());

    for (size_t i = 0; i < state.size(); ++i) {
        const auto& h = state[i];
        ReportRow row;

        QString ip   = QString::fromStdWString(addr_to_wstring(h.addr));
        bool hasAddr = (h.addr.Ipv4.sin_family != AF_UNSPEC);
        QString name = QString::fromStdWString(h.getName());
        if (hasAddr && name.isEmpty()) name = ip;
        // Hops without an address show the engine's status text ("Request
        // timed out.", "Destination host unreachable.", ...) so active ICMP
        // refusals are visible instead of hiding behind a dash. Before the
        // first probe completes there is no status yet, hence the dash.
        if (!hasAddr && name.isEmpty()) name = QStringLiteral("-");

        row.errorRow = !hasAddr && name != QLatin1String("-");

        row.cells[ColHop]      = QString::number(static_cast<int>(i) + 1);
        row.cells[ColAsn]      = hasAddr ? asnLookup(ip, h.addr.Ipv6.sin6_family == AF_INET6)
                                          : QStringLiteral("-");
        row.cells[ColHostname] = name;
        row.cells[ColIp]       = hasAddr ? ip : QStringLiteral("-");

        if (h.xmit == 0) {
            for (int c = ColLoss; c < ColCount; ++c) row.cells[c] = QStringLiteral("-");
        } else {
            int loss = 100 - (100 * h.returned / h.xmit);
            row.cells[ColLoss] = QString::number(loss);
            row.cells[ColSent] = QString::number(h.xmit);
            row.cells[ColRecv] = QString::number(h.returned);
            row.cells[ColBest] = h.returned == 0 ? QStringLiteral("-") : QString::number(h.best);
            row.cells[ColAvrg] = h.returned == 0 ? QStringLiteral("-") : QString::number(h.getAvg());
            row.cells[ColWrst] = h.returned == 0 ? QStringLiteral("-") : QString::number(h.worst);
            row.cells[ColLast] = h.returned == 0 ? QStringLiteral("-") : QString::number(h.last);
            row.cells[ColJttr] = h.returned < 2  ? QStringLiteral("-") : QString::number(h.getJitter());
        }

        rows.push_back(row);
    }
    return rows;
}

// ==========================================================================
//  Text / JSON export
// ==========================================================================

QString buildJsonReport(const QString& target,
                         const QDateTime& testStarted,
                         qint64 durationMs,
                         const std::vector<ReportRow>& rows,
                         const std::vector<OpenMTRHostInfo>& state)
{
    static const QStringList keys = {
        "hop", "asn", "hostname", "ip", "loss", "sent", "recv",
        "best", "avrg", "wrst", "last", "jttr"
    };
    QJsonArray hops;
    for (size_t i = 0; i < rows.size(); ++i) {
        const ReportRow& row = rows[i];
        QJsonObject o;
        for (int c = 0; c < ColCount; ++c) {
            const QString& v = row.cells[c];
            if (v.isEmpty() || v == QLatin1String("-")) {
                o[keys[c]] = QJsonValue::Null;
                continue;
            }
            bool numeric = false;
            const int n = v.toInt(&numeric);
            o[keys[c]] = numeric ? QJsonValue(n) : QJsonValue(v);
        }
        if (i < state.size() && state[i].altCount > 0) {
            o["alt_ip"]    = QString::fromStdWString(addr_to_wstring(state[i].altAddr));
            o["alt_count"] = state[i].altCount;
        }
        hops.append(o);
    }
    QJsonObject root;
    root["target"]           = target;
    root["test_started"]     = (testStarted.isValid() ? testStarted : QDateTime::currentDateTime()).toString(Qt::ISODate);
    root["duration_seconds"] = static_cast<qint64>(durationMs / 1000);
    root["generated"]        = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["hops"]             = hops;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString buildCsvReport(const std::vector<ReportRow>& rows)
{
    QString out = COLUMNS.join(',') + "\n";
    for (const auto& row : rows) {
        QStringList cells;
        for (int c = 0; c < ColCount; ++c) {
            QString val = row.cells[c];
            // Neutralise spreadsheet formula injection, but only in the
            // hostname column — the one place a remote party controls the
            // text: a hop along the path can name itself "=cmd|..." via
            // reverse DNS and Excel/LibreOffice would run the cell as a
            // formula when this CSV is opened. Risky leading characters get
            // the OWASP-recommended quote prefix. The other columns are
            // app-generated numbers, addresses and fixed strings, where
            // prefixing could only distort data.
            if (c == ColHostname && !val.isEmpty() && val != QLatin1String("-")) {
                const QChar c0 = val.at(0);
                if (c0 == QLatin1Char('=') || c0 == QLatin1Char('+') ||
                    c0 == QLatin1Char('-') || c0 == QLatin1Char('@') ||
                    c0 == QLatin1Char('\t'))
                    val.prepend(QLatin1Char('\''));
            }
            if (val.contains(',') || val.contains('"'))
                val = "\"" + val.replace("\"", "\"\"") + "\"";
            cells << val;
        }
        out += cells.join(',') + "\n";
    }
    return out;
}

QString buildTextReport(const QString& target,
                         const QDateTime& testStarted,
                         qint64 durationMs,
                         const std::vector<ReportRow>& rows,
                         const std::vector<OpenMTRHostInfo>& state,
                         bool traceIsV6)
{
    const int NCOLS = ColCount;
    std::vector<int> W(NCOLS);
    for (int c = 0; c < NCOLS; ++c) {
        int w = static_cast<int>(COLUMNS[c].length());
        for (const auto& row : rows) {
            const int len = static_cast<int>(row.cells[c].length());
            if (len > w) w = len;
        }
        W[c] = w;
    }
    auto pad = [](const QString& s, int w) { return s.leftJustified(w, ' '); };
    // Every column reads better right-aligned (numbers/percentages lining up
    // on their ones digit) except Hostname and IP, which are free-form text
    // of very different lengths — those stay left-aligned. Applies to the
    // header row too, so it lines up with the data underneath it.
    auto padRight = [](const QString& s, int w) { return s.rightJustified(w, ' '); };
    auto isRightAligned = [](int c) { return c != ColHostname && c != ColIp; };
    auto cell = [&](const QString& s, int c) {
        return isRightAligned(c) ? padRight(s, W[c]) : pad(s, W[c]);
    };
    QString sep = "+";
    for (int c = 0; c < NCOLS; ++c) sep += QString(W[c] + 2, '-') + "+";
    QString out;
    out += "OpenMTR Export\n";
    out += QString("Target  : %1\n").arg(target);
    out += QString("Date    : %1\n").arg((testStarted.isValid() ? testStarted : QDateTime::currentDateTime())
                                              .toString("yyyy-MM-dd hh:mm:ss"));
    out += QString("Duration: %1\n\n").arg(formatDuration(durationMs));
    out += sep + "\n";
    QString hdr = "|";
    for (int c = 0; c < NCOLS; ++c) hdr += " " + cell(COLUMNS[c], c) + " |";
    out += hdr + "\n" + sep + "\n";
    for (const auto& row : rows) {
        QString line = "|";
        for (int c = 0; c < NCOLS; ++c) {
            if (row.errorRow && c == ColHostname) {
                // Error rows merge Hostname+IP into one left-aligned field
                // spanning the combined width of both columns — still text,
                // so it stays left-aligned like Hostname/IP normally would.
                const int wSpan = W[ColHostname] + W[ColIp] + 3;
                line += " " + pad(row.cells[ColHostname], wSpan) + " |";
                ++c;   // the IP column is consumed by the span
                continue;
            }
            line += " " + cell(row.cells[c], c) + " |";
        }
        out += line + "\n";
    }
    out += sep + "\n";
    // Anomalous probe completions (a reply carrying an uncounted ICMP status,
    // or a soft failure of the send call) are invisible in the table but
    // matter when diagnosing unexplained single-packet losses — list them.
    {
        QString notes;
        for (size_t i = 0; i < state.size(); ++i)
            if (state[i].altCount > 0)
                notes += QString("  Hop %1: replies also arrived from %2 (%3 time(s)) \u2014 route change or per-packet load balancing\n")
                             .arg(i + 1)
                             .arg(QString::fromStdWString(addr_to_wstring(state[i].altAddr)))
                             .arg(state[i].altCount);
        for (size_t i = 0; i < state.size(); ++i)
            if (state[i].anomalyCount > 0)
                notes += QString("  Hop %1: %2 probe(s) ended with unexpected ICMP status/error: %3\n")
                             .arg(i + 1).arg(state[i].anomalyCount).arg(describeStatus(state[i].anomalyLast, traceIsV6));
        if (!notes.isEmpty())
            out += "\nNotes:\n" + notes;
    }
    return out;
}

// ==========================================================================
//  Warm-up settling
// ==========================================================================

bool warmupRouteSettled(WarmupFingerprint& fp,
                         const std::vector<OpenMTRHostInfo>& state,
                         int checkHops,
                         qint64 elapsedMs)
{
    constexpr qint64 kWarmupDeadlineMs = 12000;
    // The route fingerprint (hop count plus every hop's address) must hold
    // steady for this many 250 ms ticks before we call it settled.
    // Responding hops probe on a ~1 s cycle, so the window spans one full
    // cycle with margin; silent hops (5 s timeout cycles) are covered by the
    // per-hop guard below rather than by this window.
    constexpr int kWarmupStableTicks = 5;
    const bool deadlineReached = elapsedMs >= kWarmupDeadlineMs;

    if (deadlineReached)
        return true;

    // Fingerprint the discovered route: hop count plus every hop's address.
    // Requiring the whole fingerprint — not just the count — to hold steady
    // also catches middle hops that are still filling in while the
    // destination has already answered. An undiscovered route (hop count
    // pinned at the ceiling) needs no special case: the fingerprint plus the
    // per-hop guard below settle it as well, so even an unreachable target
    // gets a complete, stable reveal. The deadline is only a backstop for
    // routes that never stop changing.
    QByteArray newFp;
    newFp.append(static_cast<char>(checkHops));
    for (int i = 0; i < checkHops; ++i) {
        const auto& a = state[i].addr;
        if (a.Ipv4.sin_family == AF_INET)
            newFp.append(reinterpret_cast<const char*>(&a.Ipv4.sin_addr),
                         sizeof(a.Ipv4.sin_addr));
        else if (a.Ipv6.sin6_family == AF_INET6)
            newFp.append(reinterpret_cast<const char*>(&a.Ipv6.sin6_addr),
                         sizeof(a.Ipv6.sin6_addr));
        else
            newFp.append('\0');
    }
    // Any change to the route restarts the stability window.
    if (newFp != fp.fingerprint) {
        fp.fingerprint  = newFp;
        fp.stableCount  = 1;
        return false;
    }
    if (++fp.stableCount < kWarmupStableTicks)
        return false;

    // Every hop we are about to show must have either answered at least once
    // or sat through two full probe windows without answering. xmit
    // increments only after a probe completes, so for a silent hop
    // xmit >= 2 means two complete 5 s timeouts — it is almost certainly a
    // genuinely silent hop, not one whose first reply is still in flight and
    // would pop into the table right after the reveal.
    for (int i = 0; i < checkHops; ++i)
        if (state[i].returned == 0 && state[i].xmit < 2)
            return false;

    return true;
}

bool dnsAndAsnSettled(DnsSettle& d,
                       const std::vector<OpenMTRHostInfo>& state,
                       bool asnPending,
                       qint64 elapsedMs)
{
    // Consider reverse-DNS "settled" when every addressed hop shows a name
    // other than its bare IP, or when no new name has appeared for a few
    // ticks (the remaining hops simply have no PTR record). This never
    // blocks on a result that isn't coming.
    int addressed = 0, named = 0;
    for (const auto& h : state) {
        if (h.addr.Ipv4.sin_family == AF_UNSPEC) continue;
        ++addressed;
        if (h.getName() != addr_to_wstring(h.addr)) ++named;
    }
    bool dnsSettled;
    if (addressed == 0 || named >= addressed)  dnsSettled = true;
    else if (d.dnsSeen < 0)     { d.dnsSeen = named; d.dnsStall = 0; dnsSettled = false; }
    else if (named > d.dnsSeen) { d.dnsSeen = named; d.dnsStall = 0; dnsSettled = false; }
    else                          dnsSettled = (++d.dnsStall >= 3);   // ~450 ms without a new name

    if ((asnPending || !dnsSettled) && elapsedMs < 16000)
        return false;
    return true;
}
