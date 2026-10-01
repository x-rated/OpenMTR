// ==========================================================================
//  report_core.h — shared report logic (GUI + CLI)
//
//  Everything here is deliberately free of QWidget/QTableWidget: it is the
//  single source of truth for "what a hop's report row looks like", "when
//  has the route settled enough to start counting" and "how a report
//  renders as text/JSON/CSV". MainWindow (the GUI) and cli.cpp (headless
//  `--report` mode) both call into this instead of each having their own
//  copy, specifically so the two can never drift apart.
// ==========================================================================

#pragma once

#include "tracer.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QDateTime>

#include <functional>
#include <vector>

// ==========================================================================
//  Columns
// ==========================================================================

// Logical report/table columns. The two trailing spacer columns the GUI adds
// (ColCount and ColCount + 1) are a MainWindow-only rendering detail, not
// part of the report itself. Must stay in sync with the COLUMNS string list.
enum Column {
    ColHop = 0, ColAsn, ColHostname, ColIp, ColLoss, ColSent, ColRecv,
    ColBest, ColAvrg, ColWrst, ColLast, ColJttr,
    ColCount
};

extern const QStringList COLUMNS;

// ==========================================================================
//  Per-hop report row
// ==========================================================================

// One hop's already-formatted cell text, exactly as it should be displayed
// or exported — dashes for "no data yet", merged error text for a hop that
// never got an address. `errorRow` mirrors the GUI's merged-cell case: no
// address, but the engine has status text to show instead.
struct ReportRow {
    QString cells[ColCount];
    bool    errorRow = false;
};

// A lookup callback: given an IP and whether it's v6, return its ASN (or
// "-" while still resolving/unknown). MainWindow passes its ASN cache;
// cli.cpp passes its own (see asncache.h) — same shape, different cache.
using AsnLookupFn = std::function<QString(const QString& ip, bool ipv6)>;

// Turns a raw engine snapshot into display-ready rows: hop numbering,
// address/name fallback, the merged "-" rules, loss percent, and RTT/jitter
// columns. Used by MainWindow::updateTable() to paint the results table and
// by the CLI report builder, so both show identical figures for identical
// engine state.
std::vector<ReportRow> computeReportRows(const std::vector<OpenMTRHostInfo>& state,
                                          const AsnLookupFn& asnLookup);

// A probe's status for the tooltip and the export: the engine's own
// sentence plus the numeric code, e.g. "Destination host unreachable,
// code 11003".
QString describeStatus(unsigned long status, bool ipv6);

// Format a duration as H:MM:SS (or M:SS under an hour). Shared by the
// window-title elapsed display and every report's Duration field, so
// they never disagree on formatting.
QString formatDuration(qint64 ms);

// ==========================================================================
//  Text / JSON export
// ==========================================================================

// Renders a fixed-width ASCII box report, identical in content and layout
// to MainWindow's Export/Copy output.
QString buildTextReport(const QString& target,
                         const QDateTime& testStarted,
                         qint64 durationMs,
                         const std::vector<ReportRow>& rows,
                         const std::vector<OpenMTRHostInfo>& state,
                         bool traceIsV6);

// Renders the machine-readable JSON report: one object per hop, numbers as
// numbers, missing values ("-") as null.
QString buildJsonReport(const QString& target,
                         const QDateTime& testStarted,
                         qint64 durationMs,
                         const std::vector<ReportRow>& rows,
                         const std::vector<OpenMTRHostInfo>& state);

// Renders the report as CSV: header row from COLUMNS, one data row per hop.
// Same formula-injection guard and quoting as the GUI's Export dialog.
QString buildCsvReport(const std::vector<ReportRow>& rows);

// ==========================================================================
//  Warm-up settling (route discovery, then ASN/reverse-DNS grace)
// ==========================================================================

// Route-fingerprint stability tracker (MainWindow's m_warmupFp), pulled out
// so a blocking CLI loop can drive the same state machine as the GUI's
// QTimer-chained one.
struct WarmupFingerprint {
    QByteArray fingerprint;
    int        stableCount = 0;
};

// True once the discovered route has held steady for the required number of
// ticks (or the deadline was reached) AND every checked hop has either
// answered or exhausted its two-timeout guard. False means "call again
// shortly" (every ~250 ms, same cadence as the GUI). `elapsedMs` is time
// since the trace started (m_elapsed in the GUI).
bool warmupRouteSettled(WarmupFingerprint& fp,
                         const std::vector<OpenMTRHostInfo>& state,
                         int checkHops,
                         qint64 elapsedMs);

// Reverse-DNS settling tracker for the post-route-settle grace window.
// Mirrors the GUI's dnsSeen/dnsStall pair in onWarmupEnd()'s ASN/DNS poll.
struct DnsSettle {
    int dnsSeen  = -1;
    int dnsStall = 0;
};

// True once every addressed hop has a resolved name (or reverse-DNS has
// stalled for a few ticks with nothing new arriving) AND no ASN lookup is
// still pending. `elapsedMs` is the same clock as warmupRouteSettled's,
// still counted from trace start — matches the GUI's single 16 s overall
// cap covering both phases.
bool dnsAndAsnSettled(DnsSettle& d,
                       const std::vector<OpenMTRHostInfo>& state,
                       bool asnPending,
                       qint64 elapsedMs);
