#include "media/ProcessRunner.h"

#include <QProcess>

namespace tsm {

QString ProcessResult::errorSummary() const
{
    if (!started) {
        const QString detail = QString::fromUtf8(stdErr).trimmed();
        return detail.isEmpty() ? QStringLiteral("无法启动进程") : QStringLiteral("无法启动进程: ") + detail;
    }
    if (timedOut)
        return QStringLiteral("进程超时");
    const QStringList lines = QString::fromUtf8(stdErr).split(u'\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty())
            return line;
    }
    return QStringLiteral("退出码 %1").arg(exitCode);
}

namespace ProcessRunner {

void hideConsoleWindow(QProcess& process)
{
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#else
    Q_UNUSED(process);
#endif
}

ProcessResult run(const QString& program, const QStringList& arguments, int timeoutMs)
{
    ProcessResult result;
    QProcess process;
    hideConsoleWindow(process);
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) {
        result.stdErr = process.errorString().toUtf8();
        return result;
    }
    result.started = true;
    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(3000);
    }
    result.stdOut = process.readAllStandardOutput();
    result.stdErr = process.readAllStandardError();
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return result;
}

} // namespace ProcessRunner
} // namespace tsm
