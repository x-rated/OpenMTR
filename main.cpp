// ==========================================================================
//  main.cpp — application entry point
//
//  Brings up Winsock, creates the Qt application object and the main window,
//  runs the event loop, and shuts Winsock down again on exit.
// ==========================================================================

// ==========================================================================
//  Includes
// ==========================================================================

// Project headers.
#ifdef _WIN32
// Windows sockets — the two defines must come before <winsock2.h>.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#endif

// Project headers.
#include "MainWindow.h"
#include "version.h"

// Qt.
#include <QtPlugin>
#include <QtWidgets/QApplication>

// Static Qt plugins linked into the executable (static Qt build).
#ifdef _WIN32
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
#endif

// ==========================================================================
//  Entry point
// ==========================================================================

// Program entry point. Order matters: Winsock first (the tracer needs it),
// then the Qt application and window, then the event loop. Winsock is torn
// down only after the loop returns so background sockets stay valid.
int main(int argc, char *argv[]) {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
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

  MainWindow w;
  w.show();
  int ret = app.exec();

#ifdef _WIN32
  WSACleanup();
#endif
  return ret;
}
