// ==========================================================================
//  report.h — report building blocks shared by the window and report mode
//
//  Everything that turns engine snapshots into what a user reads: the table
//  columns, the text of every cell, the ASN lookup, and the text and JSON
//  reports. MainWindow uses it for the live table, Copy and Export; the
//  headless report mode (cli.cpp) for its output — so both always print the
//  same thing. Needs QtCore only, no widgets.
// ==========================================================================

#pragma once

#include "tracer.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

// ==========================================================================
//  Columns
// ==========================================================================

// Logical report / table columns. The window's table adds two trailing
// spacer columns at ColCount and ColCount + 1 (the first of them is moved to
// visual position 0 as the left edge padding). Must stay in sync with
// reportColumns().
enum Column {
    ColHop = 0, ColAsn, ColHostname, ColIp, ColLoss, ColSent, ColRecv,
    ColBest, ColAvrg, ColWrst, ColLast, ColJttr,
    ColCount
};

// Header text of every column, in Column order.
const QStringList& reportColumns();

// ==========================================================================
//  Rows
// ==========================================================================

// One hop as it is shown and reported: the text of every cell ("-" where
// there is no value) plus the details the report lists under "Notes".
struct ReportRow {
    QStringList   cells;              // ColCount entries
    // The hop has no address: Hostname holds the engine's status text
    // ("Request timed out.", ...), shown across the Hostname and IP columns.
    bool          statusRow    = false;
    QString       altIp;              // multipath / route change, if altCount > 0
    int           altCount     = 0;
    int           anomalyCount = 0;
    unsigned long anomalyLast  = 0;
};

// The row for hop `index` (0-based) of an engine snapshot. `asn` is the hop's
// AS number, empty when unknown or not looked up.
ReportRow makeReportRow(int index, const OpenMTRHostInfo& h, const QString& asn);

// ==========================================================================
//  Reports
// ==========================================================================

// What the report header says about the test.
struct ReportInfo {
    QString   title = QStringLiteral("OpenMTR Export");   // first line of the text report
    QString   target;                                     // as the user typed it
    QString   address;                                    // resolved address; empty = not shown
    QDateTime started;                                    // start of the counting window
    qint64    durationMs = 0;
    bool      ipv6 = false;
};

// Fixed-width ASCII box, each column sized to its content (clipboard, .txt,
// report mode).
QString buildTextReport(const ReportInfo& info, const std::vector<ReportRow>& rows);

// Machine-readable report: one object per hop, numbers as numbers, missing
// values as null. `extra` is merged into the top-level object.
QString buildJsonReport(const ReportInfo& info, const std::vector<ReportRow>& rows,
                        const QJsonObject& extra = QJsonObject());

// ==========================================================================
//  Helpers
// ==========================================================================

// H:MM:SS, or M:SS under an hour.
QString formatDuration(qint64 ms);

// A probe status for people, with its number, e.g. "Destination host
// unreachable, code 11003".
QString describeStatus(unsigned long status, bool ipv6);

// A hop's AS number via Team Cymru's DNS service; empty for private and
// link-local ranges or when the lookup fails. Blocking — never call it on a
// GUI thread.
QString lookupAsn(const QString& ip, bool ipv6);
