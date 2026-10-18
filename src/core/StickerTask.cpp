#include "core/StickerTask.h"

#include "core/Spec.h"

#include <QCoreApplication>
#include <QUuid>
#include <cmath>

namespace tsm {

StickerTask StickerTask::fromMedia(const QString& path, const MediaInfo& info)
{
    StickerTask task;
    task.id = newId();
    task.inputPath = path;
    task.info = info;
    if (info.isVideo && info.duration > 0) {
        // 新素材默认片段 = 整段视频
        task.startTime = 0.0;
        task.endTime = info.duration;
    }
    task.resetRuntimeState();
    return task;
}

QString StickerTask::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

double StickerTask::effectiveDuration() const
{
    if (startTime && endTime && *endTime > *startTime)
        return *endTime - *startTime;
    return info.duration > 0 ? info.duration : spec::kDefaultDuration;
}

bool StickerTask::isTrimmed() const
{
    if (!startTime || !endTime)
        return false;
    constexpr double eps = 1e-3;
    return std::abs(*startTime) > eps || std::abs(*endTime - info.duration) > eps;
}

QString StickerTask::clipLabel() const
{
    if (!startTime || !endTime)
        return {};
    return QStringLiteral("[%1s - %2s]").arg(*startTime, 0, 'f', 3).arg(*endTime, 0, 'f', 3);
}

QString StickerTask::cropTag() const
{
    const bool hasRadius = cropRadius > 0.0;
    if (!crop && !hasRadius)
        return {};
    QStringList parts;
    if (hasRadius)
        parts << shapeLabel(cropRadius);
    if (crop)
        parts << QStringLiteral("%1×%2").arg(crop->width()).arg(crop->height());
    return QStringLiteral("[✂️ ") + parts.join(u' ') + u']';
}

QString StickerTask::outputExtension() const
{
    return info.isVideo ? QStringLiteral("webm") : QStringLiteral("webp");
}

void StickerTask::resetRuntimeState()
{
    status = TaskStatus::Pending;
    statusText = defaultStatusText(status);
    progress = 0;
    outputSize = 0;
    outputPath.clear();
}

QString shapeLabel(double radius)
{
    if (radius <= 0.0)
        return QCoreApplication::translate("StickerTask", "直角");
    if (radius >= 1.0)
        return QCoreApplication::translate("StickerTask", "圆形");
    return QCoreApplication::translate("StickerTask", "圆角 %1%").arg(qRound(radius * 100));
}

QString defaultStatusText(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Pending: return QCoreApplication::translate("StickerTask", "等待转换");
    case TaskStatus::Running: return QCoreApplication::translate("StickerTask", "正在转码...");
    case TaskStatus::Succeeded: return QCoreApplication::translate("StickerTask", "转换成功");
    case TaskStatus::Failed: return QCoreApplication::translate("StickerTask", "转换失败");
    case TaskStatus::Cancelled: return QCoreApplication::translate("StickerTask", "已取消");
    }
    return {};
}

} // namespace tsm
