#include <QtWidgets/QApplication>
#include "MainWindow.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

// Static Qt build: musíme explicitně importovat platform + style plugin,
// jinak linker vyhodí "no QPA platform" chybu za běhu.
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)

int main(int argc, char* argv[])
{
    // Initialise Winsock once for the lifetime of the process.
    // This must happen before any getaddrinfo / DNS calls, including
    // the ones in MainWindow::onStartStop before OpenMTRNet is created.
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // DPI awareness is declared in app.manifest (PerMonitorV2).
    QApplication app(argc, argv);
    app.setApplicationName("OpenMTR");
    app.setApplicationVersion("1.0.0");
    app.setStyle("Fusion");

    MainWindow w;
    w.show();
    int ret = app.exec();

    WSACleanup();
    return ret;
}
