#include "media/FFmpegTools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace tsm::FFmpegTools {

namespace {
QString locate(const QString& name)
{
#ifdef Q_OS_WIN
    const QString executable = name + QStringLiteral(".exe");
#else
    const QString executable = name;
#endif
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(executable);
    if (QFileInfo::exists(bundled))
        return bundled;
    return QStandardPaths::findExecutable(name);
}
} // namespace

QString ffmpegPath()
{
    static const QString path = locate(QStringLiteral("ffmpeg"));
    return path;
}

QString ffprobePath()
{
    static const QString path = locate(QStringLiteral("ffprobe"));
    return path;
}

} // namespace tsm::FFmpegTools
