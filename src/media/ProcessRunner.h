#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QProcess;

namespace tsm {

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray stdOut;
    QByteArray stdErr;

    bool ok() const { return started && !timedOut && exitCode == 0; }
    // 用于状态栏 / 日志的具体失败原因（stderr 最后一行）
    QString errorSummary() const;
};

namespace ProcessRunner {

// 子进程不弹出命令行窗口
void hideConsoleWindow(QProcess& process);
// 同步运行（仅在工作线程调用）
ProcessResult run(const QString& program, const QStringList& arguments, int timeoutMs = 60000);

} // namespace ProcessRunner
} // namespace tsm
