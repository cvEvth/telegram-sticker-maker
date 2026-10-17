#pragma once

#include "core/StickerList.h"

namespace tsm::ListLoader {

// 找文件（需求 9.2）：先按原路径（绝对路径），找不到再按清单所在目录的相对路径，
// 再找不到就在清单同目录按文件名找。都找不到返回空。
QString resolvePath(const QString& rawPath, const QString& baseDir, const QString& fileName);

enum class Outcome { Loaded, Missing, Failed };

struct Result {
    Outcome outcome = Outcome::Failed;
    StickerTask task;
    QString error;
};

// 还原一条记录：找文件 → 文件未变且有缓存的解析结果就直接用，否则用 ffprobe 解析。
// 会启动子进程，只在工作线程调用。
Result load(const StickerList::Entry& entry, const QString& baseDir);

} // namespace tsm::ListLoader
