#pragma once

#include <QString>
#include <optional>

// 读写 WebM 文件头（Segment → Info → Duration）里记录的总时长。
// 用于"3 秒破限"：保留全部帧，只把头部时长改写为 2.99 秒（需求 5.7）。
namespace tsm::WebmDuration {

std::optional<double> read(const QString& path);
// 原位改写，不改变文件大小
bool write(const QString& path, double seconds, QString* error = nullptr);

} // namespace tsm::WebmDuration
