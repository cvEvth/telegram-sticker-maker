#pragma once

#include "core/StickerTask.h"

#include <atomic>
#include <functional>
#include <memory>

// 导出"已裁切的转换前文件"（需求 9.1 模式 B）：按片段起止剪切并应用裁切、圆角、镜像，
// 保持高画质，不压到贴纸码率。
namespace tsm::SourceTrimmer {

enum class Method {
    CopyWhole,   // 原样复制
    StreamCut,   // 不重新编码直接剪切（失败再保留全部流，再失败就复制整个文件）
    Reencode,    // 视频重新编码为高画质 VP9 WebM（保留透明）
    RenderImage, // 图片渲染一帧后按扩展名保存
};

struct Decision {
    Method method = Method::CopyWhole;
    QString extension; // 小写，不含点
};

// 输出扩展名规则：
//  - 视频：有裁切、圆角或镜像 → webm；整段 → 原样复制；否则原扩展名直接剪切
//  - 图片：有圆角 → png；有裁切或镜像 → png/jpg/webp 保留原扩展名，其他转 png；都没有 → 原样复制
Decision decide(const StickerTask& task);

struct Result {
    bool ok = false;
    bool cancelled = false;
    QString file;          // 生成的文件；原样复制时为源文件本身
    QString extension;
    bool isSourceCopy = false;
    QString note;          // 成功时的状态说明
    QString error;
};

using Progress = std::function<void(int percent, const QString& text)>;

// 同步执行，只在工作线程调用。outputBase 不含扩展名。
Result run(const StickerTask& task, const QString& workDir, const QString& outputBase,
           const std::shared_ptr<std::atomic_bool>& cancel, const Progress& progress = {});

} // namespace tsm::SourceTrimmer
