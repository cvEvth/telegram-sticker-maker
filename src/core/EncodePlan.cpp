#include "core/EncodePlan.h"

#include "core/Format.h"

#include <QCoreApplication>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

namespace tsm::EncodePlan {

namespace {
constexpr std::array<double, 6> kFpsTiers{30, 24, 20, 18, 15, 12};
constexpr std::array<double, 5> kCandidateFps{30, 24, 20, 18, 15};

int roundTo(double value, int multiple)
{
    return int(std::lround(value / multiple)) * multiple;
}

QSize fitLongSide(QSize source, int side, int multiple, int minSide)
{
    if (source.width() <= 0 || source.height() <= 0)
        return {side, side};
    const bool landscape = source.width() >= source.height();
    const double ratio = landscape ? double(source.height()) / source.width() : double(source.width()) / source.height();
    const int shortSide = std::clamp(roundTo(side * ratio, multiple), minSide, side);
    return landscape ? QSize(side, shortSide) : QSize(shortSide, side);
}

QString kb(qint64 bytes)
{
    return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral("KB");
}

QString fpsText(double fps)
{
    return formatFps(fps);
}
} // namespace

QSize videoContentSize(QSize source, OutputMode mode)
{
    if (mode == OutputMode::Emoji)
        return fitLongSide(source, spec::kEmojiSide, 2, 2);
    return fitLongSide(source, spec::kStickerSide, 16, 16);
}

QSize staticContentSize(QSize source, OutputMode mode)
{
    if (mode == OutputMode::Emoji)
        return fitLongSide(source, spec::kEmojiSide, 1, 1);
    return fitLongSide(source, spec::kStickerSide, 1, 1);
}

QSize canvasSize(QSize content, OutputMode mode)
{
    return mode == OutputMode::Emoji ? QSize(spec::kEmojiSide, spec::kEmojiSide) : content;
}

qint64 targetBytes(OutputMode mode)
{
    return mode == OutputMode::Emoji ? spec::kEmojiVideoTarget : spec::kStickerVideoTarget;
}

qint64 videoLimitBytes(OutputMode mode)
{
    return mode == OutputMode::Emoji ? spec::kEmojiLimit : spec::kStickerVideoLimit;
}

qint64 staticLimitBytes(OutputMode mode)
{
    return mode == OutputMode::Emoji ? spec::kEmojiLimit : spec::kStickerStaticLimit;
}

double bitsPerPixel(qint64 target, double duration, QSize frame, double fps)
{
    const double pixels = duration * frame.width() * frame.height() * fps;
    return pixels > 0 ? target * 8.0 / pixels : 0.0;
}

int bitrateKbps(qint64 target, double duration)
{
    const double kbps = duration > 0 ? target * 8.0 / duration / 1000.0 : kMaxBitrateKbps;
    return std::clamp(int(std::lround(kbps)), kMinBitrateKbps, kMaxBitrateKbps);
}

int constantQualityFor(EncodePreset preset, OutputMode mode)
{
    const bool emoji = mode == OutputMode::Emoji;
    switch (preset) {
    case EncodePreset::Quality: return emoji ? 24 : 18;
    case EncodePreset::Fast: return emoji ? 26 : 20;
    }
    return 20;
}

double nextLowerFps(double fps)
{
    for (const double tier : kFpsTiers) {
        if (tier < fps - 0.01)
            return tier;
    }
    return fps; // 已经是最低档（或原生更低的帧率）
}

double nextHigherFps(double fps, double cap)
{
    for (auto it = kFpsTiers.rbegin(); it != kFpsTiers.rend(); ++it) {
        if (*it > fps + 0.01)
            return std::min(*it, cap);
    }
    return std::min(fps, cap);
}

double chooseFps(const Input& in, QSize canvas, qint64 target)
{
    const double source = in.sourceFps > 0 ? in.sourceFps : spec::kMaxFps;
    const double cap = std::min(spec::kMaxFps, source);
    const auto bppAt = [&](double fps) { return bitsPerPixel(target, in.duration, canvas, fps); };

    double fps = 0;
    bool anyCandidate = false;
    for (const double candidate : kCandidateFps) {
        if (candidate > cap + 0.01)
            continue;
        anyCandidate = true;
        if (bppAt(candidate) >= kBppTarget) {
            fps = candidate;
            break;
        }
    }
    if (!anyCandidate)
        fps = cap; // 原生低帧率保持原样，不升帧
    else if (fps == 0)
        fps = bppAt(15) < kBppEmergency ? 12 : 15;

    if (in.motion && in.duration > kShortClipSeconds) {
        if (*in.motion < kStillMotion)
            fps = cap;
        else if (*in.motion < kMediumMotion)
            fps = nextHigherFps(fps, cap);
    }
    return fps;
}

Plan plan(const Input& in)
{
    Plan p;
    p.content = videoContentSize(in.source, in.mode);
    p.canvas = canvasSize(p.content, in.mode);
    p.sourceFps = in.sourceFps;
    p.duration = in.duration > 0 ? in.duration : spec::kDefaultDuration;
    p.targetBytes = targetBytes(in.mode);
    p.limitBytes = videoLimitBytes(in.mode);

    Input effective = in;
    effective.duration = p.duration;
    p.fps = chooseFps(effective, p.canvas, p.targetBytes);
    p.bpp = bitsPerPixel(p.targetBytes, p.duration, p.canvas, p.fps);
    p.bitrateKbps = bitrateKbps(p.targetBytes, p.duration);
    p.theoreticalKbps = p.bitrateKbps;
    p.constantQuality = p.duration <= kShortClipSeconds && p.bpp >= kBppConstantQuality;
    p.crf = constantQualityFor(in.preset, in.mode);
    p.twoPass = in.preset != EncodePreset::Fast;

    if (in.motion && p.duration > kShortClipSeconds) {
        const double m = *in.motion;
        const QString name = m < kStillMotion ? QCoreApplication::translate("EncodePlan", "静止") : m < kMediumMotion ? QCoreApplication::translate("EncodePlan", "中运动") : QCoreApplication::translate("EncodePlan", "高运动");
        p.motionLabel = name + QStringLiteral(" ") + QString::number(m, 'f', 3);
    }
    return p;
}

QString Plan::summary() const
{
    const QString fpsPart = sourceFps > 0 && std::abs(sourceFps - fps) > 0.01
        ? fpsText(std::min(sourceFps, 999.0)) + QStringLiteral("→") + fpsText(fps) + QStringLiteral("fps")
        : fpsText(fps) + QStringLiteral("fps");
    QStringList parts;
    parts << QStringLiteral("%1s %2x%3 %4").arg(QString::number(duration, 'f', 1)).arg(canvas.width()).arg(canvas.height()).arg(fpsPart);
    if (!motionLabel.isEmpty())
        parts << motionLabel;
    const QString rate = constantQuality ? QStringLiteral("CQ crf%1 ≤%2kbps").arg(crf).arg(bitrateKbps)
                                         : QStringLiteral("ABR %1kbps").arg(bitrateKbps);
    parts << rate + QStringLiteral(" bpp=") + QString::number(bpp, 'f', 3);
    return parts.join(QStringLiteral(" | "));
}

Decision evaluate(const Plan& plan, RetryState& state, qint64 actualBytes)
{
    Decision d;
    if (actualBytes > plan.limitBytes) {
        if (state.retries >= kMaxRetries) {
            d.verdict = Verdict::Fail;
            d.note = QCoreApplication::translate("EncodePlan", "无法将体积压缩至 %1KB 以内").arg(plan.limitBytes / 1024);
            return d;
        }
        const int oldRate = state.bitrateKbps;
        const double scaled = oldRate * (double(plan.limitBytes) / actualBytes) * 0.97;
        const int newRate = std::max(kMinBitrateKbps, std::min(int(std::floor(scaled)), oldRate - 1));
        if (state.overshoots == 0) {
            d.note = QCoreApplication::translate("EncodePlan", "体积 %1 超限，码率 %2k→%3k...").arg(kb(actualBytes)).arg(oldRate).arg(newRate);
        } else {
            const double oldFps = state.fps;
            state.fps = nextLowerFps(oldFps);
            d.note = QCoreApplication::translate("EncodePlan", "体积 %1 超限，%2→%3fps 重压...").arg(kb(actualBytes), fpsText(oldFps), fpsText(state.fps));
        }
        state.bitrateKbps = newRate;
        ++state.overshoots;
        ++state.retries;
        d.verdict = Verdict::Retry;
        return d;
    }

    const bool underfilled = actualBytes < plan.targetBytes * 0.9;
    if (underfilled && !plan.constantQuality && !state.raised && state.overshoots == 0 && state.retries < kMaxRetries
        && actualBytes > 0) {
        const int oldRate = state.bitrateKbps;
        const double wanted = oldRate * double(plan.targetBytes) / actualBytes;
        const int ceiling = int(std::floor(plan.theoreticalKbps * 1.08));
        const int newRate = std::min({int(std::floor(wanted)), ceiling, kMaxBitrateKbps});
        if (newRate >= oldRate * 1.05) {
            state.bitrateKbps = newRate;
            state.raised = true;
            ++state.retries;
            d.verdict = Verdict::Retry;
            d.note = QCoreApplication::translate("EncodePlan", "体积 %1 偏低，上调码率 %2k→%3k...").arg(kb(actualBytes)).arg(oldRate).arg(newRate);
            return d;
        }
    }
    d.verdict = Verdict::Accept;
    return d;
}

} // namespace tsm::EncodePlan
