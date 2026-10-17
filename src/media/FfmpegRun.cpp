#include "media/FfmpegRun.h"

#include "media/FFmpegTools.h"
#include "media/ProcessRunner.h"

#include <QProcess>
#include <algorithm>

namespace tsm::FfmpegRun {

Result run(const QStringList& args, double duration, const std::shared_ptr<std::atomic_bool>& cancel,
           const std::function<void(double)>& onProgress)
{
    Result result;
    const QString ffmpeg = FFmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) {
        result.error = QStringLiteral("找不到 ffmpeg");
        return result;
    }
    QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-progress"),
                     QStringLiteral("pipe:1"), QStringLiteral("-nostats")};
    full << args;

    QProcess process;
    ProcessRunner::hideConsoleWindow(process);
    process.start(ffmpeg, full);
    if (!process.waitForStarted(10000)) {
        result.error = process.errorString();
        return result;
    }

    QByteArray pending;
    while (process.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            process.kill();
            process.waitForFinished(3000);
            result.cancelled = true;
            result.error = QStringLiteral("已取消");
            return result;
        }
        process.waitForReadyRead(100);
        pending += process.readAllStandardOutput();
        qsizetype nl;
        while ((nl = pending.indexOf('\n')) >= 0) {
            const QByteArray line = pending.left(nl).trimmed();
            pending.remove(0, nl + 1);
            if (onProgress && duration > 0 && line.startsWith("out_time_us="))
                onProgress(std::clamp(line.mid(12).toDouble() / 1e6 / duration, 0.0, 1.0));
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QStringList lines = QString::fromUtf8(process.readAllStandardError()).split(u'\n', Qt::SkipEmptyParts);
        result.error = lines.isEmpty() ? QStringLiteral("ffmpeg 退出码 %1").arg(process.exitCode()) : lines.last().trimmed();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace tsm::FfmpegRun
