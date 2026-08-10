// ==========================================================================
//  main.cpp — application entry point
//
//  On Windows, brings up Winsock before anything else (the tracer needs it)
//  and tears it down on exit. Creates the Qt application object and the main
//  window, then runs the event loop.
// ==========================================================================

// ==========================================================================
//  Includes
// ==========================================================================

// Project headers.
#include "MainWindow.h"
#include "version.h"

#ifdef _WIN32
// Windows sockets — the two defines must come before <winsock2.h>.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#endif

// Qt.
#include <QtWidgets/QApplication>
#include <QtPlugin>

#ifdef Q_OS_LINUX
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileDevice>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#endif

// Static Qt plugins linked into the executable — Windows only, since that's
// the only platform built as a fully static Qt binary. macOS links Qt
// dynamically and bundles the Cocoa platform plugin via macdeployqt instead
// (see .github/workflows/build.yml).
#ifdef _WIN32
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifdef Q_OS_LINUX
// ==========================================================================
//  AppImage desktop-entry self-integration (Linux only)
// ==========================================================================
//
// GNOME Shell (Wayland especially, but Mutter/X11 too) never reads window
// icons directly — there's no Wayland equivalent of X11's _NET_WM_ICON. It
// only resolves a taskbar/dock icon by matching the window's app_id
// (== setDesktopFileName() below) or WM_CLASS against an *installed*
// .desktop file on XDG_DATA_DIRS. A bare AppImage has none, so it falls
// back to a generic gear icon — true of every AppImage, which is why tools
// like AppImageLauncher exist to register one on first run.
//
// This does that registration itself, without a separate tool or root: it
// writes a desktop entry + icon into the user's XDG data dir
// (~/.local/share), Exec pointing at wherever this AppImage currently
// lives. Safe on every launch — it only touches disk when the content
// actually needs to change.
static void integrateAppImageDesktopEntry()
{
    // Set by the AppImage runtime itself to the path of the running
    // .AppImage file; absent for a plain (non-AppImage) Linux build, in
    // which case there's nothing to self-integrate.
    const QString appImagePath = qEnvironmentVariable("APPIMAGE");
    if (appImagePath.isEmpty())
        return;

    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return;

    const QString desktopDir = dataHome + QStringLiteral("/applications");
    const QString iconDir    = dataHome + QStringLiteral("/icons/hicolor/256x256/apps");
    QDir().mkpath(desktopDir);
    QDir().mkpath(iconDir);

    // Exec's argument is quoted since the AppImage's path may contain
    // spaces (e.g. sitting in "Downloads" under a non-English locale, or
    // just a folder name with spaces); %u is the standard desktop-entry
    // placeholder for a single optional URL/file argument.
    const QString desktopContents =
        QStringLiteral(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=OpenMTR\n"
            "Comment=Combined traceroute and ping network diagnostic tool\n"
            "Exec=\"%1\" %u\n"
            "Icon=openmtr\n"
            "Categories=Network;Utility;\n"
            "StartupWMClass=OpenMTR\n"
            "Terminal=false\n")
            .arg(appImagePath);

    const QString desktopPath = desktopDir + QStringLiteral("/openmtr.desktop");
    QFile existingDesktop(desktopPath);
    bool desktopNeedsWrite = true;
    if (existingDesktop.open(QIODevice::ReadOnly | QIODevice::Text)) {
        desktopNeedsWrite = QString::fromUtf8(existingDesktop.readAll()) != desktopContents;
        existingDesktop.close();
    }
    if (desktopNeedsWrite) {
        QFile out(desktopPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            out.write(desktopContents.toUtf8());
            out.close();
            // Desktop entries must be executable to be trusted/launched by
            // some file managers' "Allow Launching" prompt.
            QFile::setPermissions(desktopPath,
                out.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        }
    }

    // The icon itself never changes between versions, so it's only written
    // once rather than compared byte-for-byte on every launch.
    const QString iconPath = iconDir + QStringLiteral("/openmtr.png");
    if (!QFile::exists(iconPath))
        QFile::copy(QStringLiteral(":/openmtr.png"), iconPath);
}
#endif

// ==========================================================================
//  Entry point
// ==========================================================================

// Program entry point. Order matters: Winsock first (the tracer needs it),
// then the Qt application and window, then the event loop. Winsock is torn
// down only after the loop returns so background sockets stay valid.
int main(int argc, char* argv[])
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

#ifdef Q_OS_LINUX
    // Many Linux desktops set QT_QPA_PLATFORMTHEME=gtk3, which pulls the
    // system GTK theme's palette and style hints (e.g. "dialog buttons show
    // icons instead of text") into every Qt widget — not just genuinely
    // native dialogs, but Qt's own non-native ones too (like the Export
    // dialog, see MainWindow::onExport()). On a desktop with a missing or
    // broken GTK icon theme this shows up as unreadable black panels and
    // icon-only buttons. The app already forces its own Fusion style and
    // does its own light/dark theming, so it doesn't need this platform
    // theme integration; disabling it keeps the look consistent regardless
    // of whatever GTK theme (if any) happens to be installed.
    qputenv("QT_QPA_PLATFORMTHEME", QByteArray());
#endif

    // At fractional Windows display scaling (125%, 150%, 175%...) Qt's
    // default "PassThrough" DPI policy positions widgets at fractional
    // physical-pixel coordinates, which blurs every 1px border in the app
    // (toolbar buttons, dialog buttons, everything). Rounding the scale
    // factor to the nearest integer keeps all widget geometry on whole
    // pixels, so borders render crisp. Must be set before QApplication
    // is constructed.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Round);

    QApplication app(argc, argv);
    app.setApplicationName("OpenMTR");
    app.setApplicationVersion(OPENMTR_VERSION);
    app.setStyle("Fusion");

#ifdef Q_OS_LINUX
    // Ties this running process to openmtr.desktop (installed as
    // .../applications/openmtr.desktop — see CMakeLists.txt). Without it,
    // several window managers — GNOME Shell under Wayland in particular —
    // can't match the window back to its desktop entry and fall back to a
    // generic taskbar icon instead of the one MainWindow sets
    // (see MainWindow::updateAppIcon()).
    app.setDesktopFileName(QStringLiteral("openmtr"));

    // No-op for a plain Linux install (see function comment) — only does
    // anything when actually running as an AppImage.
    integrateAppImageDesktopEntry();
#endif

    MainWindow w;
    w.show();
    int ret = app.exec();

#ifdef _WIN32
    WSACleanup();
#endif
    return ret;
}
