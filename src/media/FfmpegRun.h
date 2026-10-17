#pragma once

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>

namespace tsm::FfmpegRun {

struct Result {
    bool ok = false;
    bool cancelled = false;
    QString error; // stderr 最后一行
};

// 同步运行 ffmpeg（只在工作线程调用）。duration > 0 时按 out_time 回报 0~1 进度；cancel 置位时结束进程。
Result run(const QStringList& args, double duration, const std::shared_ptr<std::atomic_bool>& cancel,
           const std::function<void(double fraction)>& onProgress = {});

} // namespace tsm::FfmpegRun
