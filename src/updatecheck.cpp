#include "updatecheck.h"

#if OPENMTR_ENABLE_UPDATE_CHECK

#include "version.h"

#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QVersionNumber>

// Done via a short-lived `curl` subprocess rather than Qt6Network in-process:
// statically linking Qt6Network purely for a version check gives Windows
// binaries a network+TLS profile that AV/ML heuristics associate with
// downloader/C2 behaviour, and has triggered false positives here before.
// Routing the HTTPS request through curl.exe — a binary AV/EDR vendors
// already allowlist — means OpenMTR.exe itself never performs a TLS
// handshake or carries a network stack, so there's nothing left for that
// heuristic to key off. Qt6Network is not linked by this project at all as
// a result. QProcess::start() is used (never a shell), and the curl path is
// always an explicit absolute path resolved ourselves — never a bare
// "curl" string, since unqualified PATH lookup is platform-inconsistent and
// has had a real security advisory against it.
UpdateInfo checkForUpdateBlocking()
{
    UpdateInfo result;

#ifdef Q_OS_WIN
    // Shipped inbox since Windows 10 build 17063 (the 1803 feature update);
    // hardcoded rather than PATH-searched so a same-named binary earlier in
    // PATH can never be picked up instead.
    static const QString curlPath = QStringLiteral("C:/Windows/System32/curl.exe");
#elif defined(Q_OS_MACOS)
    // Apple ships its own curl at this fixed path on every supported macOS
    // release.
    static const QString curlPath = QStringLiteral("/usr/bin/curl");
#else
    // No single well-known path is guaranteed across Linux distros, so
    // resolve it the same way a shell's PATH lookup would — once, via Qt's
    // own API — rather than trusting an unqualified "curl" string to
    // QProcess's own PATH search.
    static const QString curlPath = QStandardPaths::findExecutable(QStringLiteral("curl"));
#endif
    if (curlPath.isEmpty() || !QFileInfo::exists(curlPath))
        return result;

    QProcess proc;
    proc.setProgram(curlPath);
    proc.setArguments({
        QStringLiteral("-s"),                 // silent - no progress meter on stdout
        QStringLiteral("-L"),                 // follow redirects
        QStringLiteral("--max-time"), QStringLiteral("10"),
        QStringLiteral("-A"), QStringLiteral("OpenMTR"),
        QStringLiteral("-H"), QStringLiteral("Accept: application/vnd.github+json"),
        QStringLiteral("https://api.github.com/repos/x-rated/OpenMTR/releases/latest"),
    });
    // start(), never startCommand()/a shell — arguments are passed as an
    // argv array, so there is no shell-quoting/injection surface even
    // though every argument here is a compile-time literal anyway.
    proc.start();
    if (!proc.waitForFinished(11000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return result;

    const auto doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isObject()) return result;
    const QJsonObject obj = doc.object();

    QString tag = obj.value(QStringLiteral("tag_name")).toString();
    if (tag.startsWith(QLatin1Char('v'))) tag.remove(0, 1);
    if (tag.isEmpty() || obj.value(QStringLiteral("draft")).toBool()
                       || obj.value(QStringLiteral("prerelease")).toBool())
        return result;

    const QVersionNumber latest  = QVersionNumber::fromString(tag);
    const QVersionNumber current = QVersionNumber::fromString(QStringLiteral(OPENMTR_VERSION));
    if (latest.isNull() || latest <= current) return result;

    const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();
    // Belt-and-braces even though callers that open this URL already refuse
    // anything but http/https before ever handing it to the OS.
    if (!htmlUrl.startsWith(QLatin1String("https://github.com/")))
        return result;

    result.available = true;
    result.version    = tag;
    result.url         = htmlUrl;
    return result;
}

#endif  // OPENMTR_ENABLE_UPDATE_CHECK
