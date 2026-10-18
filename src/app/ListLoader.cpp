#include "app/ListLoader.h"

#include "core/MediaKinds.h"
#include "media/MediaProbe.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace tsm::ListLoader {

QString resolvePath(const QString& rawPath, const QString& baseDir, const QString& fileName)
{
    const QString raw = QDir::fromNativeSeparators(rawPath);
    if (QDir::isAbsolutePath(raw) && QFileInfo(raw).isFile())
        return QFileInfo(raw).absoluteFilePath();
    if (!baseDir.isEmpty()) {
        const QString relative = QDir(baseDir).absoluteFilePath(raw);
        if (QFileInfo(relative).isFile())
            return QDir::cleanPath(relative);
        const QString name = fileName.isEmpty() ? QFileInfo(raw).fileName() : fileName;
        const QString sameDir = QDir(baseDir).filePath(name);
        if (!name.isEmpty() && QFileInfo(sameDir).isFile())
            return QDir::cleanPath(sameDir);
        const QString byRawName = QDir(baseDir).filePath(QFileInfo(raw).fileName());
        if (QFileInfo(byRawName).isFile())
            return QDir::cleanPath(byRawName);
    }
    return {};
}

Result load(const StickerList::Entry& entry, const QString& baseDir)
{
    Result result;
    const QString path = resolvePath(entry.task.inputPath, baseDir, entry.task.info.fileName);
    if (path.isEmpty()) {
        result.outcome = Outcome::Missing;
        result.error = QCoreApplication::translate("ListLoader", "找不到源文件");
        return result;
    }

    MediaInfo info;
    const QFileInfo fi(path);
    const bool cacheValid = entry.hasMedia && fi.size() == entry.mediaFileSize
        && fi.lastModified().toMSecsSinceEpoch() == entry.mediaMtime;
    if (cacheValid) {
        info = entry.task.info;
        info.fileName = fi.fileName();
        info.fileSize = fi.size();
        const MediaKind kind = MediaKinds::classify(path);
        info.isVideo = kind == MediaKind::Video || kind == MediaKind::AnimatedImage;
    } else {
        const ProbeResult probe = MediaProbe::probe(path);
        if (!probe.ok) {
            result.error = probe.error;
            return result;
        }
        info = probe.info;
    }

    StickerTask task = StickerTask::fromMedia(path, info);
    const StickerTask& saved = entry.task;
    task.emoji = saved.emoji;
    task.keywords = saved.keywords;
    task.crop = saved.crop;
    task.cropRadius = saved.cropRadius;
    task.mirror = saved.mirror;
    task.clipGroupId = saved.clipGroupId;
    task.clipId = saved.clipId;
    if (info.isVideo) {
        // 片段区间：没有记录时为整段；收进视频时长内
        const double duration = info.duration;
        if (saved.startTime)
            task.startTime = std::clamp(*saved.startTime, 0.0, duration);
        if (saved.endTime)
            task.endTime = std::clamp(*saved.endTime, 0.0, duration);
        if (task.startTime && task.endTime && *task.endTime <= *task.startTime)
            task.endTime = duration > *task.startTime ? std::optional<double>(duration) : std::nullopt;
    } else {
        task.startTime.reset();
        task.endTime.reset();
    }
    if (task.crop) {
        const QRect frame(0, 0, info.width, info.height);
        if (!frame.contains(*task.crop))
            task.crop = task.crop->intersected(frame);
        if (task.crop->width() < 2 || task.crop->height() < 2)
            task.crop.reset();
    }
    result.outcome = Outcome::Loaded;
    result.task = task;
    return result;
}

} // namespace tsm::ListLoader
