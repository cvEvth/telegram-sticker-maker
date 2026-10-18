#pragma once

#include "core/StickerTask.h"

#include <QDateTime>
#include <functional>

// sticker_list.json（本工具的列表）与 stickers.json（转换输出）的读写。
// 会话文件格式同 sticker_list.json，使用绝对路径，另附可选的 media 字段缓存解析结果。
namespace tsm::StickerList {

inline constexpr auto kKind = "sticker_maker_list";
inline constexpr int kVersion = 1;

struct WriteOptions {
    QString exportMode = QStringLiteral("session");     // session / sources / trimmed
    QDateTime exportedAt;                               // 无效时省略 exported_at（会话用，便于判断内容是否变化）
    bool includeMedia = false;                          // 附带解析结果，恢复时文件未变就不必重新解析
    std::function<QString(const StickerTask&)> pathFor; // 默认写绝对路径
    std::function<QString(const StickerTask&)> proxyFor; // 预览副本相对路径（可为空）
};

QByteArray write(const QList<StickerTask>& tasks, const WriteOptions& options);

struct Entry {
    StickerTask task;         // inputPath 为清单里的原始路径（可能是相对路径）
    QString proxy;            // 清单里的预览副本路径
    bool hasMedia = false;    // 有缓存的解析结果
    qint64 mediaFileSize = 0;
    qint64 mediaMtime = 0;    // 毫秒
};

enum class Kind { StickerList, StickersJson };

struct ParseResult {
    bool ok = false;
    Kind kind = Kind::StickerList;
    QList<Entry> entries;
    QString error; // "列表文件格式无效"
};

ParseResult parse(const QByteArray& json);

} // namespace tsm::StickerList
