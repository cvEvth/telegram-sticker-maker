#pragma once

#include <QString>

namespace tsm {

struct MediaInfo {
    QString fileName;
    qint64 fileSize = 0;
    bool isVideo = false;         // 视频与动图（GIF / APNG / 动态 WebP）都走视频贴纸流程
    bool isAnimatedImage = false;
    int width = 0;
    int height = 0;
    double duration = 0.0;        // 秒
    double fps = 0.0;
    bool hasAlpha = false;
    QString container;
    QString videoCodec;

    bool isValid() const { return width > 0 && height > 0; }
};

} // namespace tsm
