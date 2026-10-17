#pragma once

#include "core/StickerTask.h"

#include <QImage>
#include <atomic>
#include <memory>

// 给 AI 识别用的图片（需求 7.2）：只取最终贴纸区域（先应用镜像、裁切框、圆角，透明处铺白底）。
// 视频 / 动图取多帧拼成横向胶片条（每帧高 ≤ 200，间隔 4px 灰色，总宽 ≤ 768）；静态图缩到 256 以内。
namespace tsm::AiFrameGrabber {

inline constexpr int kFrameHeight = 200;
inline constexpr int kStripMaxWidth = 768;
inline constexpr int kGap = 4;
inline constexpr int kStaticMaxSide = 256;
inline constexpr int kJpegQuality = 80;

// 把若干帧（已铺白底）拼成胶片条
QImage composeStrip(const QList<QImage>& frames);

struct Result {
    bool ok = false;
    QImage image;
    QByteArray jpeg;
    QString error;
};

// 启动 ffmpeg，只在工作线程调用
Result grab(const StickerTask& task, const QString& workDir, const std::shared_ptr<std::atomic_bool>& cancel = {});

} // namespace tsm::AiFrameGrabber
