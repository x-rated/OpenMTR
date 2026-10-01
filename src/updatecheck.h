// ==========================================================================
//  updatecheck.h — GitHub release check (GUI + CLI)
//
//  OPENMTR_ENABLE_UPDATE_CHECK is set once, in CMakeLists.txt, and read here
//  and by every call site (MainWindow.cpp, cli.cpp) — one switch controls
//  all of them at once, so the feature can never be on in one and off in
//  the other. See CMakeLists.txt for why it defaults to 0.
// ==========================================================================

#pragma once

#include <QtCore/QString>

#ifndef OPENMTR_ENABLE_UPDATE_CHECK
#define OPENMTR_ENABLE_UPDATE_CHECK 0
#endif

#if OPENMTR_ENABLE_UPDATE_CHECK

struct UpdateInfo {
    bool    available = false;
    QString version;   // e.g. "1.5.0", only meaningful if available
    QString url;        // release page; always an https://github.com/... link
};

// One-shot check against the GitHub Releases API, done via a short-lived
// `curl` subprocess (see updatecheck.cpp for why not Qt6Network). Blocking —
// callers that care about not stalling their own thread run this on a
// background thread themselves; this function has no threading of its own.
// Returns available=false for any reason at all (curl missing, offline,
// GitHub down, an unexpected response shape, a draft/prerelease, or simply
// already up to date) — this is a courtesy notice, not something worth
// distinguishing failure reasons for.
UpdateInfo checkForUpdateBlocking();

#endif  // OPENMTR_ENABLE_UPDATE_CHECK
