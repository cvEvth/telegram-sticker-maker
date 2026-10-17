#pragma once

#include "core/StickerList.h"

#include <atomic>
#include <memory>

// 导入入口（需求 9.2）：zip / json / 文件夹 → 清单文件 → 条目列表
namespace tsm::ImportSource {

inline constexpr auto kListFileName = "sticker_list.json";
inline constexpr auto kStickersFileName = "stickers.json";

// 在文件夹里找 sticker_list.json 或 stickers.json（也查找下一级子文件夹）
QString findManifestIn(const QString& dir);

// zip 解压目录：导入缓存下按 zip 路径 + 修改时间 + 大小生成的标识
QString extractDirFor(const QString& zipPath, const QString& importCacheDir);

struct Prepared {
    bool ok = false;
    QString manifestPath;
    QString baseDir; // 清单所在目录，相对路径以此为准
    StickerList::ParseResult list;
    bool reusedExtraction = false; // 同一个 zip 再次导入，直接复用之前解压的内容
    QString error;
};

// 定位并解析清单；zip 解压到导入缓存。可能耗时，只在工作线程调用。
Prepared prepare(const QString& path, const QString& importCacheDir,
                 const std::shared_ptr<std::atomic_bool>& cancel = {});

// 附带的预览副本装回缓存目录（已有则跳过）。返回是否装入。
bool installProxy(const QString& proxyFile, const QString& source, const QString& proxyCacheDir);

} // namespace tsm::ImportSource
