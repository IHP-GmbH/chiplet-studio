// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Logger.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QtGlobal>

#include <cstdlib>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcGds, "chiplet.gds")
Q_LOGGING_CATEGORY(lcKLayout, "chiplet.klayout")
Q_LOGGING_CATEGORY(lcStackup, "chiplet.stackup")

namespace chiplet {
namespace {

// File descriptor of the session log. After the (default) redirect it is also
// duplicated onto stdout/stderr, so raw writes from libraries land here too.
int g_logFd = -1;
// Saved real terminal stderr, used for the startup banner and to surface
// warnings/fatals even when the terminal is otherwise silenced.
int g_realStderrFd = -1;
bool g_echoConsole = false;
bool g_active = false;
QString g_logPath;
QtMessageHandler g_prevHandler = nullptr;

bool envIsTruthy(const char* name)
{
    QByteArray v = qgetenv(name).trimmed().toLower();
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

const char* levelName(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg:    return "DEBUG";
        case QtInfoMsg:     return "INFO ";
        case QtWarningMsg:  return "WARN ";
        case QtCriticalMsg: return "ERROR";
        case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

void writeAll(int fd, const char* data, qsizetype len)
{
#if defined(Q_OS_UNIX)
    if (fd < 0)
        return;
    while (len > 0) {
        ssize_t n = ::write(fd, data, static_cast<size_t>(len));
        if (n <= 0)
            break;
        data += n;
        len -= n;
    }
#else
    Q_UNUSED(fd);
    Q_UNUSED(data);
    Q_UNUSED(len);
#endif
}

void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const char* cat = (ctx.category && *ctx.category) ? ctx.category : "default";

    QString line;
    line.reserve(msg.size() + 96);
    line += '[';
    line += ts;
    line += QLatin1String("] [");
    line += QLatin1String(levelName(type));
    line += QLatin1String("] [");
    line += QLatin1String(cat);
    line += QLatin1String("] ");
    line += msg;
    // File/line is only populated in debug builds; include it for warnings and
    // above, where it most helps triage.
    if (ctx.file && type >= QtWarningMsg) {
        line += QLatin1String(" (");
        line += QLatin1String(ctx.file);
        line += ':';
        line += QString::number(ctx.line);
        line += ')';
    }
    line += '\n';

    const QByteArray utf8 = line.toUtf8();
    writeAll(g_logFd, utf8.constData(), utf8.size());

    // Keep the launching terminal quiet by default: only genuine errors and
    // fatals are surfaced there (so a crash is never invisible). Everything
    // else, including the per-frame warning chatter, stays in the log file.
    // CHIPLET_LOG_CONSOLE=1 tees the full stream for development.
    if (g_realStderrFd >= 0 && (g_echoConsole || type >= QtCriticalMsg))
        writeAll(g_realStderrFd, utf8.constData(), utf8.size());

    if (type == QtFatalMsg)
        ::abort();
}

} // namespace

bool Logger::init(int argc, char** argv, const QString& appVersion)
{
#if !defined(Q_OS_UNIX)
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    Q_UNUSED(appVersion);
    return false;
#else
    if (g_active)
        return true;
    if (qEnvironmentVariableIsSet("CHIPLET_NO_LOGFILE"))
        return false;

    // Log directory priority: CHIPLET_LOG_DIR override > a logs/ dir next to the
    // project file argument (so a .chiplet in outputs/ logs to outputs/logs/,
    // alongside the rest of the flow's logs) > the current directory.
    QString dir;
    if (qEnvironmentVariableIsSet("CHIPLET_LOG_DIR")) {
        dir = qEnvironmentVariable("CHIPLET_LOG_DIR");
    } else {
        dir = QDir::currentPath();
        for (int i = 1; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg.startsWith('-'))
                continue; // skip option flags; the first file arg is the project
            const QString parent = QFileInfo(arg).absolutePath();
            if (!parent.isEmpty() && QDir(parent).mkpath(QStringLiteral("logs")))
                dir = QDir(parent).absoluteFilePath(QStringLiteral("logs"));
            break; // only the first non-flag argument (the .chiplet path)
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    QString base = QStringLiteral("chiplet-studio_%1.log")
                       .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString path = QDir(dir).absoluteFilePath(base);
    // Avoid clobbering a same-second sibling launch.
    if (QFileInfo::exists(path)) {
        path = QDir(dir).absoluteFilePath(
            QStringLiteral("chiplet-studio_%1_%2.log")
                .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss")))
                .arg(::getpid()));
    }

    int fd = ::open(path.toLocal8Bit().constData(),
                    O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return false; // keep default Qt behavior

    g_logFd = fd;
    g_realStderrFd = ::dup(STDERR_FILENO);
    g_echoConsole = envIsTruthy("CHIPLET_LOG_CONSOLE");
    g_logPath = path;

    if (!g_echoConsole) {
        // Capture raw stdout/stderr (std::cerr, library and driver chatter) into
        // the log and keep the launching terminal clean.
        ::dup2(g_logFd, STDOUT_FILENO);
        ::dup2(g_logFd, STDERR_FILENO);
    }

    // One line on the real terminal so the user knows where the session went.
    const QByteArray banner =
        QStringLiteral("Chiplet Studio: session log -> %1\n").arg(path).toUtf8();
    writeAll(g_realStderrFd, banner.constData(), banner.size());

    // Structured header for the file.
    QString cmdline;
    for (int i = 0; i < argc; ++i) {
        if (i)
            cmdline += ' ';
        cmdline += QString::fromLocal8Bit(argv[i]);
    }
    const QByteArray header =
        QStringLiteral(
            "==== Chiplet Studio %1 session start ====\n"
            "  time: %2\n"
            "  pid : %3\n"
            "  cmd : %4\n"
            "=========================================\n")
            .arg(appVersion,
                 now.toString(Qt::ISODateWithMs),
                 QString::number(::getpid()),
                 cmdline)
            .toUtf8();
    writeAll(g_logFd, header.constData(), header.size());

    g_prevHandler = qInstallMessageHandler(messageHandler);
    g_active = true;
    return true;
#endif
}

void Logger::shutdown()
{
#if defined(Q_OS_UNIX)
    if (!g_active)
        return;
    const QByteArray footer =
        QStringLiteral("==== session end: %1 ====\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
            .toUtf8();
    writeAll(g_logFd, footer.constData(), footer.size());
    qInstallMessageHandler(g_prevHandler);
    g_active = false;
#endif
}

QString Logger::logFilePath()
{
    return g_logPath;
}

} // namespace chiplet
