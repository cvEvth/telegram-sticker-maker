#pragma once

#include "core/MediaInfo.h"

namespace tsm {

struct ProbeResult {
    bool ok = false;
    MediaInfo info;
    QString error;
};

namespace MediaProbe {

// 解析素材信息。会启动 ffprobe，只在工作线程调用。
ProbeResult probe(const QString& path);

} // namespace MediaProbe
} // namespace tsm
