// ==========================================================================
//  versioninfo.h — the app's version line (GUI + CLI)
// ==========================================================================

#pragma once

#include <QtCore/QString>

// "1.4.0 DEV (AMD64) · Qt 6.8.1" — used identically by MainWindow's About
// dialog and the CLI's --version, so the two can never show different text
// for the same build.
QString openMtrVersionLine();
