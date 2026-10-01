// ==========================================================================
//  cli.h — headless report mode (`OpenMTR --report ...`)
// ==========================================================================

#pragma once

namespace cli {

// Parses argv, runs a trace and prints one report to stdout, then returns
// a process exit code (see cli.cpp for the full list). Never touches Qt
// Widgets and never constructs a QApplication — main.cpp calls this
// *instead of* creating the GUI when "--report" is present on the command
// line, so this path works with no display at all (SSH, cron, CI).
int run(int argc, char** argv);

// Prints the one-line version (same text as the About dialog) to stdout and
// returns the process exit code. Lives here rather than in main.cpp because
// on Windows even this needs the console-vs-redirected-output handling
// (writeText in cli.cpp) that non-ASCII characters in that text require.
int printVersion();

}  // namespace cli
