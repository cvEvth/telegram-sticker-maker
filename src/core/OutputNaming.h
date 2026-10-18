#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>

// 输出命名规则
namespace tsm::OutputNaming {

// 去掉文件名非法字符与控制字符
QString sanitizeEmoji(const QString& emoji);
// "001_😂.webm"；无 Emoji 时 "001.webm"
QString stickerFileName(int index, const QString& emoji, const QString& extension);
// "TG_Stickers_20261017_143000"
QString timestampedName(const QString& prefix, const QDateTime& time);
// 与 takenLower（已占用名称的小写形式）重名时追加 _2、_3……
QString uniqueName(const QString& fileName, const QSet<QString>& takenLower);
// 同上，但检查磁盘上的目录内容
QString uniqueFileName(const QString& dir, const QString& fileName);

} // namespace tsm::OutputNaming
