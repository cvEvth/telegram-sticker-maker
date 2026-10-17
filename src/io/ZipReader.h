#pragma once

#include <QString>
#include <atomic>
#include <memory>

namespace tsm::ZipReader {

// 把整个 ZIP 解压到 destDir（路径可含中文）。拒绝绝对路径和 ".." 条目，防止写到目标目录外。
// cancel 置位时中途停止并返回 false（error = "已取消"）。
bool extractAll(const QString& zipPath, const QString& destDir, QString* error = nullptr,
                const std::shared_ptr<std::atomic_bool>& cancel = {});

} // namespace tsm::ZipReader
