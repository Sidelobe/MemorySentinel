//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2023 Lorenz Bucher - all rights reserved
//  https://github.com/Sidelobe/MemorySentinel

// Let Catch provide main():
#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

// MARK: - Windows Crash Dialog Suppression
#if defined(_MSC_VER)

#include <windows.h>
#include <crtdbg.h>
#include <stdlib.h>

// A crash or failed assert otherwise opens a modal dialog and the process never exits -> looks like
// a hanging test in CI. Route everything to stderr and fail fast instead.
struct WindowsCrashDialogSuppressor
{
    WindowsCrashDialogSuppressor()
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        for (int report : {_CRT_ASSERT, _CRT_ERROR, _CRT_WARN}) {
            _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
        }
    }
};
static WindowsCrashDialogSuppressor g_windowsCrashDialogSuppressor;

#endif
