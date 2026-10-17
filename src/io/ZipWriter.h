#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>

namespace tsm::ZipWriter {

struct Entry {
    QString name;     // 包内文件名（UTF-8）
    QString filePath; // 从磁盘读取；为空时使用 data
    QByteArray data;
};

// 进度 0~1；返回 false 表示取消
using ProgressFn = std::function<bool(double fraction)>;

// 写一个仅存储、不压缩的 ZIP。路径可含中文。磁盘文件直接从源文件流式写入，不整个读进内存。
// 失败或取消时删除半成品。
bool writeStored(const QString& zipPath, const QList<Entry>& entries, QString* error = nullptr,
                 const ProgressFn& progress = {});

} // namespace tsm::ZipWriter
