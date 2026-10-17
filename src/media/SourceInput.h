#pragma once

#include <QString>

// ffmpeg 实际读取的输入文件。
// FFmpeg 不能解码动态 WebP，这类文件先用 libwebp 逐帧解码，存成带透明、保留逐帧时长的
// PNG-in-MKV 中间文件（缓存目录下按 路径 + 修改时间 + 大小 标识，重复使用）。其他文件原样返回。
namespace tsm::SourceInput {

// 缓存目录（数据目录下的 preview_cache/decoded）；未设置时用系统临时目录
void setCacheDir(const QString& dir);
QString cacheDir();

// 需要时同步生成中间文件（可能耗时，只在工作线程调用）；失败时返回原路径
QString resolve(const QString& path);

// 生成中间文件；成功返回 true
bool decodeAnimatedWebp(const QString& source, const QString& output, QString* error = nullptr);

} // namespace tsm::SourceInput
