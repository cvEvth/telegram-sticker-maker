#pragma once

#include "core/EncodePlan.h"
#include "core/StickerTask.h"

#include <atomic>
#include <functional>
#include <memory>

namespace tsm {

struct EncodeRequest {
    StickerTask task;
    OutputMode mode = OutputMode::Sticker;
    EncodePreset preset = EncodePreset::Quality;
    QString workDir;    // 该任务独占的临时目录
    QString outputFile; // 成功时写到这里
};

struct EncodeResult {
    bool ok = false;
    bool cancelled = false;
    qint64 size = 0;
    QString statusText; // 成功："转换完成，体积: …"；失败：具体原因
};

using CancelFlag = std::shared_ptr<std::atomic_bool>;
using ProgressFn = std::function<void(int percent, const QString& text)>;

// 把一个任务转换为贴纸文件。同步执行，只在工作线程调用。
class StickerEncoder {
public:
    StickerEncoder(EncodeRequest request, CancelFlag cancel, ProgressFn progress);
    EncodeResult run();

    // 片段 > 4 秒时的运动量探测：均匀抽约 16 帧缩小灰度图，返回相邻帧平均亮度差（0~1）
    std::optional<double> measureMotion(double start, double duration);

private:
    struct FfmpegRun {
        bool ok = false;
        bool cancelled = false;
        QString error;
        QByteArray stdOut;
    };

    EncodeResult encodeVideo();
    EncodeResult encodeStatic();

    // 运行 ffmpeg 并把 out_time 映射到 [from, to] 百分比区间
    FfmpegRun runFfmpeg(const QStringList& args, double duration, int from, int to, const QString& text, bool captureStdout = false);
    QStringList videoArgs(const EncodePlan::Plan& plan, double fps, int bitrate, int pass, const QString& output) const;
    QStringList inputArgs(bool withMask) const;
    QString filterGraph(QSize scale, QSize canvas, double fps, bool rgba) const;
    bool writeMask(QSize size);
    QSize sourceSize() const;
    bool needsAlphaOutput(QSize content, QSize canvas) const;
    bool cancelled() const { return m_cancel && m_cancel->load(); }
    void report(int percent, const QString& text);

    EncodeRequest m_req;
    CancelFlag m_cancel;
    ProgressFn m_progress;
    QString m_maskPath;
    QString m_input; // ffmpeg 实际读取的文件（动态 WebP 为解码后的中间文件）
    double m_start = 0;
    double m_duration = 0;
};

} // namespace tsm
