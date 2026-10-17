#pragma once

#include "core/MediaInfo.h"

#include <QRect>
#include <QStringList>
#include <optional>

namespace tsm {

enum class TaskStatus { Pending, Running, Succeeded, Failed, Cancelled };

// 主列表中的一行：一个素材，或某个视频截出的一个片段
struct StickerTask {
    QString id;
    QString inputPath;
    MediaInfo info;

    QString emoji;
    QStringList keywords;
    std::optional<double> startTime;
    std::optional<double> endTime;
    std::optional<QRect> crop;   // 原始像素坐标
    double cropRadius = 0.0;     // 0 = 直角，1 = 圆形 / 胶囊
    bool mirror = false;
    QString clipGroupId;         // 同一视频截出的多个片段共享
    QString clipId;

    // 运行时状态，不写入会话
    TaskStatus status = TaskStatus::Pending;
    QString statusText;
    int progress = 0;
    qint64 outputSize = 0;
    QString outputPath;

    static StickerTask fromMedia(const QString& path, const MediaInfo& info);
    static QString newId();

    // 片段长度；没有片段信息用原视频时长，缺失按 3 秒
    double effectiveDuration() const;
    // 是否截取过（片段不是整段视频）
    bool isTrimmed() const;
    // "[1.200s - 3.700s]"
    QString clipLabel() const;
    // "[✂️ 圆角 30% 512×512]"、"[✂️ 圆形]"；无裁切无圆角时为空
    QString cropTag() const;
    QString outputExtension() const;
    void resetRuntimeState();
};

// "直角" / "圆角 N%" / "圆形"
QString shapeLabel(double radius);
QString defaultStatusText(TaskStatus status);

} // namespace tsm
