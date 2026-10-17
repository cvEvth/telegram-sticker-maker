#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace tsm::ZipWriter {

struct Entry {
    QString name;     // 包内文件名（UTF-8）
    QString filePath; // 从磁盘读取；为空时使用 data
    QByteArray data;
};

// 写一个仅存储、不压缩的 ZIP。路径可含中文。
bool writeStored(const QString& zipPath, const QList<Entry>& entries, QString* error = nullptr);

} // namespace tsm::ZipWriter
