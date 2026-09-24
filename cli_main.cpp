// ==========================================================================
//  cli_main.cpp — entry point of openmtr-cli
//
//  The command-line report tool: the same engine and report as OpenMTR's
//  --report mode (cli.cpp), built as its own small console program that
//  links QtCore only — no window, no GUI libraries, so it installs and runs
//  on servers without a desktop.
// ==========================================================================

#include "cli.h"

#ifdef _WIN32
// Windows sockets — the two defines must come before <winsock2.h>.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // The engine needs Winsock up before anything else.
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    const int rc = runReportMode(argc, argv, true);

#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}
