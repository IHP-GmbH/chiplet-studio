// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLoggingCategory>
#include <QString>

namespace chiplet {

// Session logger. Redirects Qt diagnostics plus the process stdout/stderr to a
// timestamped file in the invocation directory, so launching the tool from a
// shell (e.g. `chiplet-studio my.chiplet &`) keeps that terminal quiet instead
// of streaming the whole session log into it.
//
// Each line is structured for debug/crash triage:
//   [yyyy-MM-dd HH:mm:ss.zzz] [LEVEL] [category] message (file:line)
//
// Environment variables:
//   CHIPLET_LOG_DIR      directory for the log file (default: current directory)
//   CHIPLET_LOG_CONSOLE  if truthy, also echo to the launching terminal and do
//                        not silence raw stdout/stderr (useful for development)
//   CHIPLET_NO_LOGFILE   if set, disable file logging and keep default Qt output
class Logger {
public:
    // Install the message handler and open the session log. Call once, as the
    // very first thing in main(), before QApplication. argc/argv are recorded in
    // the header. Returns false (leaving default Qt behavior untouched) when file
    // logging is disabled, unsupported, or the file cannot be opened.
    static bool init(int argc, char** argv, const QString& appVersion);

    // Flush, write a footer and restore the previous handler. Safe to call even
    // if init() was not called or returned false.
    static void shutdown();

    // Absolute path of the active session log, or an empty string when not
    // file-logging.
    static QString logFilePath();
};

} // namespace chiplet

// Categories for the subsystems whose diagnostics previously went to raw
// std::cerr; routed through the session log handler for structured output.
Q_DECLARE_LOGGING_CATEGORY(lcGds)
Q_DECLARE_LOGGING_CATEGORY(lcKLayout)
Q_DECLARE_LOGGING_CATEGORY(lcStackup)
