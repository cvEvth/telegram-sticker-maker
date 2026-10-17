#pragma once

#include <QString>

// 定位随程序分发的 ffmpeg / ffprobe（程序目录优先，其次 PATH）
namespace tsm::FFmpegTools {

QString ffmpegPath();
QString ffprobePath();

} // namespace tsm::FFmpegTools
