#pragma once

#include <QString>
#include <optional>

namespace tsm {

// "512 B" / "183.4 KB" / "12.3 MB"
QString formatBytes(qint64 bytes);
// MM:SS.mmm，超过 1 小时为 HH:MM:SS.mmm
QString formatTimecode(double seconds);
// 支持 01:23:45.678、14:23.5、23.5、23.5s、863
std::optional<double> parseTimecode(const QString& text);
// 最多两位小数并去掉末尾的 0："2.5"、"3"
QString formatSecondsShort(double seconds);
// 29.97 → 30、23.976 → 24，其余保持
double normalizeFrameRate(double fps);
QString formatFps(double fps);

} // namespace tsm
