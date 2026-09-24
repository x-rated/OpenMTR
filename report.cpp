// ==========================================================================
//  report.cpp — report building blocks shared by the window and report mode
// ==========================================================================

#include "report.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#ifdef _WIN32
#include <windns.h>
#else
#include <QtCore/QProcess>
#endif

#include <algorithm>
#include <cstdint>
#include <memory>

// ==========================================================================
//  Columns & rows
// ==========================================================================

const QStringList& reportColumns()
{
    static const QStringList columns = {
        "Hop", "ASN", "Hostname", "IP", "Loss %", "Sent", "Recv",
        "Best ms", "Avrg ms", "Wrst ms", "Last ms", "Jttr ms"
    };
    return columns;
}

// Statistics come straight from the engine: they are reset when counting
// starts, so no baseline math is needed here.
ReportRow makeReportRow(int index, const OpenMTRHostInfo& h, const QString& asn)
{
    ReportRow row;
    const QString ip   = QString::fromStdWString(addr_to_wstring(h.addr));
    const bool hasAddr = (h.addr.Ipv4.sin_family != AF_UNSPEC);
    QString name       = QString::fromStdWString(h.getName());
    if (hasAddr && name.isEmpty()) name = ip;
    // Hops without an address show the engine's status text ("Request timed
    // out.", "Destination host unreachable.", ...) so active ICMP refusals
    // are visible instead of hiding behind a dash. Before the first probe
    // completes there is no status yet, hence the dash.
    if (!hasAddr && name.isEmpty()) name = QStringLiteral("-");

    QStringList& c = row.cells;
    c.reserve(ColCount);
    c << QString::number(index + 1)
      << (hasAddr && !asn.isEmpty() ? asn : QStringLiteral("-"))
      << name
      << (hasAddr ? ip : QStringLiteral("-"));

    if (h.xmit == 0) {
        while (c.size() < ColCount) c << QStringLiteral("-");
    } else {
        const QString dash = QStringLiteral("-");
        c << QString::number(100 - (100 * h.returned / h.xmit))
          << QString::number(h.xmit)
          << QString::number(h.returned)
          << (h.returned == 0 ? dash : QString::number(h.best))
          << (h.returned == 0 ? dash : QString::number(h.getAvg()))
          << (h.returned == 0 ? dash : QString::number(h.worst))
          << (h.returned == 0 ? dash : QString::number(h.last))
          << (h.returned <  2 ? dash : QString::number(h.getJitter()));
    }

    row.statusRow = !hasAddr && name != QLatin1String("-");
    if (hasAddr && h.altCount > 0) {
        row.altIp    = QString::fromStdWString(addr_to_wstring(h.altAddr));
        row.altCount = h.altCount;
    }
    row.anomalyCount = h.anomalyCount;
    row.anomalyLast  = h.anomalyLast;
    return row;
}

// ==========================================================================
//  Reports
// ==========================================================================

QString buildJsonReport(const ReportInfo& info, const std::vector<ReportRow>& rows,
                        const QJsonObject& extra)
{
    static const QStringList keys = {
        "hop", "asn", "hostname", "ip", "loss", "sent", "recv",
        "best", "avrg", "wrst", "last", "jttr"
    };
    QJsonArray hops;
    for (const ReportRow& r : rows) {
        QJsonObject o;
        for (int c = 0; c < ColCount; ++c) {
            const QString v = c < r.cells.size() ? r.cells[c] : QString();
            if (v.isEmpty() || v == QLatin1String("-")) {
                o[keys[c]] = QJsonValue::Null;
                continue;
            }
            bool numeric = false;
            const int n = v.toInt(&numeric);
            o[keys[c]] = numeric ? QJsonValue(n) : QJsonValue(v);
        }
        if (r.altCount > 0) {
            o["alt_ip"]    = r.altIp;
            o["alt_count"] = r.altCount;
        }
        hops.append(o);
    }
    QJsonObject root = extra;
    root["target"]           = info.target;
    // Wall-clock time the counting window began (falls back to "now" if
    // there is none) and how long it ran.
    root["test_started"]     = (info.started.isValid() ? info.started : QDateTime::currentDateTime()).toString(Qt::ISODate);
    root["duration_seconds"] = static_cast<qint64>(info.durationMs / 1000);
    root["generated"]        = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["hops"]             = hops;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString buildTextReport(const ReportInfo& info, const std::vector<ReportRow>& rows)
{
    const QStringList& columns = reportColumns();
    const int NCOLS = static_cast<int>(columns.size());
    auto cell = [](const ReportRow& r, int c) {
        return c < r.cells.size() ? r.cells[c] : QStringLiteral("-");
    };
    std::vector<int> W(NCOLS);
    for (int c = 0; c < NCOLS; ++c) {
        int w = static_cast<int>(columns[c].length());
        for (const ReportRow& r : rows) {
            const int len = static_cast<int>(cell(r, c).length());
            if (len > w) w = len;
        }
        W[c] = w;
    }
    auto pad = [](const QString& s, int w) { return s.leftJustified(w, ' '); };
    QString sep = "+";
    for (int c = 0; c < NCOLS; ++c) sep += QString(W[c] + 2, '-') + "+";
    QString out;
    out += info.title + "\n";
    out += info.address.isEmpty() || info.address == info.target
        ? QString("Target  : %1\n").arg(info.target)
        : QString("Target  : %1 (%2)\n").arg(info.target, info.address);
    out += QString("Date    : %1\n").arg((info.started.isValid() ? info.started : QDateTime::currentDateTime())
                                              .toString("yyyy-MM-dd hh:mm:ss"));
    out += QString("Duration: %1\n\n").arg(formatDuration(info.durationMs));
    out += sep + "\n";
    QString hdr = "|";
    for (int c = 0; c < NCOLS; ++c) hdr += " " + pad(columns[c], W[c]) + " |";
    out += hdr + "\n" + sep + "\n";
    for (const ReportRow& r : rows) {
        QString line = "|";
        for (int c = 0; c < NCOLS; ++c) {
            if (r.statusRow && c == ColHostname) {
                // Status rows merge Hostname+IP into one left-aligned field
                // spanning the combined width of both columns.
                const int wSpan = W[ColHostname] + W[ColIp] + 3;
                line += " " + pad(cell(r, c), wSpan) + " |";
                ++c;   // the IP column is consumed by the span
                continue;
            }
            line += " " + pad(cell(r, c), W[c]) + " |";
        }
        out += line + "\n";
    }
    out += sep + "\n";
    // Anomalous probe completions (a reply carrying an uncounted ICMP status,
    // or a soft failure of the send call) are invisible in the table but
    // matter when diagnosing unexplained single-packet losses — list them.
    QString notes;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].altCount > 0)
            notes += QString("  Hop %1: replies also arrived from %2 (%3 time(s)) — route change or per-packet load balancing\n")
                         .arg(i + 1).arg(rows[i].altIp).arg(rows[i].altCount);
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].anomalyCount > 0)
            notes += QString("  Hop %1: %2 probe(s) ended with unexpected ICMP status/error: %3\n")
                         .arg(i + 1).arg(rows[i].anomalyCount).arg(describeStatus(rows[i].anomalyLast, info.ipv6));
    if (!notes.isEmpty())
        out += "\nNotes:\n" + notes;
    return out;
}

// ==========================================================================
//  Helpers
// ==========================================================================

// Shared by the window's elapsed-time display and the report's Duration
// field, so the two never disagree on formatting.
QString formatDuration(qint64 ms)
{
    qint64 secs = ms / 1000;
    int h = static_cast<int>(secs / 3600), m = static_cast<int>((secs % 3600) / 60), s = static_cast<int>(secs % 60);
    return h > 0
        ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// The engine's own sentence and the number. The number alone meant nothing
// to anyone without ipexport.h at hand; the text alone would lose what a
// Windows report can be compared by.
QString describeStatus(unsigned long status, bool ipv6)
{
    QString text = QString::fromLatin1(OpenMTRStatusText(status, ipv6));
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return QStringLiteral("%1, code %2").arg(text).arg(status);
}

// Parsed rather than prefix-matched: the old "172." test threw away the whole
// of 172/8 when only 172.16/12 is private, hiding Google and Cloudflare, and
// it missed 100.64/10 in the other direction. inet_pton rather than
// QHostAddress because that lives in Qt6::Network, which this app does not
// link.
static bool isUnroutableForAsn(const QString& ip)
{
    const QByteArray raw = ip.toUtf8();

    in_addr v4{};
    if (inet_pton(AF_INET, raw.constData(), &v4) == 1) {
        // Explicit cast: ntohl() returns u_long on Windows, and the MSVC
        // build compiles with /W4 /WX.
        const uint32_t a = static_cast<uint32_t>(ntohl(v4.s_addr));
        return (a & 0xFF000000u) == 0x0A000000u   // 10/8
            || (a & 0xFFF00000u) == 0xAC100000u   // 172.16/12  (NOT all of 172/8)
            || (a & 0xFFFF0000u) == 0xC0A80000u   // 192.168/16
            || (a & 0xFF000000u) == 0x7F000000u   // 127/8 loopback
            || (a & 0xFFFF0000u) == 0xA9FE0000u   // 169.254/16 link-local
            || (a & 0xFFC00000u) == 0x64400000u   // 100.64/10 CGNAT
            || a == 0u;                           // 0.0.0.0
    }

    in6_addr v6{};
    if (inet_pton(AF_INET6, raw.constData(), &v6) == 1) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&v6);
        if ((b[0] & 0xFE) == 0xFC) return true;                  // fc00::/7 ULA
        if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10 link-local
        for (int i = 0; i < 16; ++i) if (b[i]) return false;
        return true;                                             // ::
    }

    return true;   // not an address we can query for
}

QString lookupAsn(const QString& ip, bool ipv6)
{
    if (ip.isEmpty() || isUnroutableForAsn(ip))
        return QString();

    QString query;
    if (!ipv6) {
        QStringList parts = ip.split('.');
        if (parts.size() != 4) return QString();
        std::reverse(parts.begin(), parts.end());
        query = parts.join('.') + ".origin.asn.cymru.com";
    } else {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET6;
        hints.ai_flags  = AI_NUMERICHOST;
        if (getaddrinfo(ip.toStdString().c_str(), nullptr, &hints, &res) != 0) return QString();
        auto resGuard = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>(res, freeaddrinfo);
        auto* sa6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
        QString hex;
        for (int b = 0; b < 16; ++b)
            hex += QString("%1").arg(sa6->sin6_addr.s6_addr[b], 2, 16, QChar('0'));
        QString reversed;
        for (int i = 31; i >= 0; --i) { reversed += hex[i]; if (i > 0) reversed += '.'; }
        query = reversed + ".origin6.asn.cymru.com";
    }

#ifdef _WIN32
    PDNS_RECORD pDnsRecord = nullptr;
    DNS_STATUS status = DnsQuery_W(query.toStdWString().c_str(), DNS_TYPE_TEXT,
        DNS_QUERY_STANDARD, nullptr, &pDnsRecord, nullptr);
    if (status != ERROR_SUCCESS || !pDnsRecord) return QString();

    QString result;
    for (PDNS_RECORD r = pDnsRecord; r; r = r->pNext) {
        if (r->wType == DNS_TYPE_TEXT && r->Data.TXT.dwStringCount > 0) {
            QString txt = QString::fromWCharArray(r->Data.TXT.pStringArray[0]);
            QString asn = txt.split('|').first().trimmed();
            if (!asn.isEmpty() && asn != "0") result = asn;
            break;
        }
    }
    DnsFree(pDnsRecord, DnsFreeRecordList);
    return result;
#else
    // No native DNS TXT API used here (unlike Windows' DnsQuery_W); shell out
    // to the standard `dig` tool instead, which every macOS install ships
    // with.
    QProcess proc;
    proc.start("dig", {"+short", "txt", query});
    if (!proc.waitForFinished(2000)) {
        // Timed out (or dig is missing): stop it here, or ~QProcess warns
        // "Destroyed while process is still running" — which report mode
        // would otherwise print into a script's stderr.
        proc.kill();
        proc.waitForFinished(1000);
        return QString();
    }
    QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (out.startsWith('"')) out.remove(0, 1);
    if (out.endsWith('"'))   out.chop(1);
    QString asn = out.split('|').first().trimmed();
    if (!asn.isEmpty() && asn != "0")
        return asn;
    return QString();
#endif
}
