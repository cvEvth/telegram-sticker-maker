#pragma once

#include "core/Spec.h"

#include <QSize>
#include <QString>
#include <optional>

// 视频贴纸的画质规划与重试规则（需求 5.3 ~ 5.6）。纯计算，不调用 FFmpeg。
namespace tsm::EncodePlan {

inline constexpr double kBppTarget = 0.055;
inline constexpr double kBppEmergency = 0.040;
inline constexpr double kBppConstantQuality = 0.10;
inline constexpr double kShortClipSeconds = 4.0;
inline constexpr double kStillMotion = 0.007;
inline constexpr double kMediumMotion = 0.014;
inline constexpr int kMaxRetries = 4;
inline constexpr int kMinBitrateKbps = 16;
inline constexpr int kMaxBitrateKbps = 2000;

// 标准贴纸：长边 512，短边按比例取 16 的倍数；自定义表情：等比缩进 100×100 内（偶数尺寸）
QSize videoContentSize(QSize source, OutputMode mode);
// 静态贴纸：长边 512，短边按比例；自定义表情：等比缩进 100×100 内
QSize staticContentSize(QSize source, OutputMode mode);
// 编码画面尺寸：自定义表情固定 100×100（四周透明填充）
QSize canvasSize(QSize content, OutputMode mode);

qint64 targetBytes(OutputMode mode);       // 252 KB / 60 KB
qint64 videoLimitBytes(OutputMode mode);   // 256 KB / 64 KB
qint64 staticLimitBytes(OutputMode mode);  // 512 KB / 64 KB

double bitsPerPixel(qint64 targetBytes, double duration, QSize frame, double fps);
// 码率 = 目标体积 × 8 ÷ 时长，限制在 16 ~ 2000 kbps
int bitrateKbps(qint64 targetBytes, double duration);
int constantQualityFor(EncodePreset preset, OutputMode mode);

// 帧率档位 30 → 24 → 20 → 18 → 15 → 12
double nextLowerFps(double fps);
double nextHigherFps(double fps, double cap);

struct Input {
    QSize source;          // 裁切后的源画面尺寸
    double sourceFps = 0;  // 已按 29.97→30 规整
    double duration = 0;   // 片段长度
    OutputMode mode = OutputMode::Sticker;
    EncodePreset preset = EncodePreset::Quality;
    std::optional<double> motion; // 仅片段 > 4 秒时探测
};

struct Plan {
    QSize content;  // 缩放后的画面
    QSize canvas;   // 实际编码尺寸
    double sourceFps = 0;
    double fps = 0;
    double duration = 0;
    qint64 targetBytes = 0;
    qint64 limitBytes = 0;
    int bitrateKbps = 0;
    int theoreticalKbps = 0; // 未经二次调整的规划码率，上调时的参照
    bool constantQuality = false;
    int crf = 0;
    bool twoPass = true;
    double bpp = 0;
    QString motionLabel;     // "静止 0.003" / "中运动 0.011" / "高运动 0.020"

    // "5.2s 512x288 30→20fps | 中运动 0.011 | ABR 387kbps bpp=0.061"
    QString summary() const;
};

double chooseFps(const Input& in, QSize canvas, qint64 target);
Plan plan(const Input& in);

// 一次编码结果之后的决定（需求 5.6）
struct RetryState {
    double fps = 0;
    int bitrateKbps = 0;
    int retries = 0;
    int overshoots = 0;
    bool raised = false;
};

enum class Verdict { Accept, Retry, Fail };

struct Decision {
    Verdict verdict = Verdict::Accept;
    QString note; // 状态提示，如 "体积 301.2KB 超限，码率 420k→340k..."
};

// 根据实际体积决定是否重试；Retry 时 state 已更新为下一次的帧率与码率
Decision evaluate(const Plan& plan, RetryState& state, qint64 actualBytes);

} // namespace tsm::EncodePlan
