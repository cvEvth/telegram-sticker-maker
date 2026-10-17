#include "media/StickerEncoder.h"

#include "core/Format.h"
#include "media/FFmpegTools.h"
#include "media/FilterGraph.h"
#include "media/ProcessRunner.h"
#include "media/WebmDuration.h"
#include "media/WebpEncoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <cmath>

namespace tsm {

namespace {
constexpr double kFastSeekThreshold = 10.0; // 起点超过 10 秒时先快速定位
constexpr double kFastSeekLead = 4.0;       // 快速定位到起点前 4 秒
constexpr int kMotionSamples = 16;
constexpr int kMotionSide = 64;

QString seconds(double v)
{
    return QString::number(v, 'f', 3);
}

QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

QString kbText(qint64 bytes)
{
    return formatBytes(bytes);
}
} // namespace

StickerEncoder::StickerEncoder(EncodeRequest request, CancelFlag cancel, ProgressFn progress)
    : m_req(std::move(request))
    , m_cancel(std::move(cancel))
    , m_progress(std::move(progress))
{
    const StickerTask& t = m_req.task;
    m_start = t.startTime.value_or(0.0);
    m_duration = t.effectiveDuration();
}

EncodeResult StickerEncoder::run()
{
    EncodeResult result;
    if (!QFileInfo::exists(m_req.task.inputPath)) {
        result.statusText = QStringLiteral("源文件不存在");
        return result;
    }
    if (FFmpegTools::ffmpegPath().isEmpty()) {
        result.statusText = QStringLiteral("找不到 ffmpeg");
        return result;
    }
    QDir().mkpath(m_req.workDir);
    if (m_req.task.cropRadius > 0) {
        const QSize content = m_req.task.info.isVideo ? EncodePlan::videoContentSize(sourceSize(), m_req.mode)
                                                      : EncodePlan::staticContentSize(sourceSize(), m_req.mode);
        if (!writeMask(content)) {
            result.statusText = QStringLiteral("无法生成圆角遮罩");
            return result;
        }
    }
    result = m_req.task.info.isVideo ? encodeVideo() : encodeStatic();
    if (cancelled()) {
        result.ok = false;
        result.cancelled = true;
        result.statusText = QStringLiteral("已取消");
    }
    return result;
}

// ---------------------------------------------------------------- 视频

EncodeResult StickerEncoder::encodeVideo()
{
    EncodeResult result;
    const MediaInfo& info = m_req.task.info;

    EncodePlan::Input input;
    input.source = sourceSize();
    input.sourceFps = normalizeFrameRate(info.fps);
    input.duration = m_duration;
    input.mode = m_req.mode;
    input.preset = m_req.preset;
    if (m_duration > EncodePlan::kShortClipSeconds) {
        report(2, QStringLiteral("分析运动量..."));
        input.motion = measureMotion(m_start, m_duration);
        if (cancelled())
            return result;
    }

    const EncodePlan::Plan plan = EncodePlan::plan(input);
    const QString summary = plan.summary();
    report(5, QStringLiteral("编码规划: ") + summary);

    EncodePlan::RetryState state;
    state.fps = plan.fps;
    state.bitrateKbps = plan.bitrateKbps;
    QString note;
    QString bestFile;
    qint64 bestSize = 0;

    for (int attempt = 0;; ++attempt) {
        const QString attemptFile = QDir(m_req.workDir).filePath(QStringLiteral("attempt_%1.webm").arg(attempt));
        const QString prefix = note.isEmpty() ? QString() : note + QStringLiteral(" ");
        const int lo = attempt == 0 ? 8 : 50;
        const int hi = 95;
        bool ok = true;
        if (plan.twoPass) {
            const int mid = lo + (hi - lo) * 2 / 5;
            const FfmpegRun p1 = runFfmpeg(videoArgs(plan, state.fps, state.bitrateKbps, 1, attemptFile), m_duration, lo,
                                           mid, prefix + QStringLiteral("第一遍分析..."));
            if (cancelled())
                return result;
            const FfmpegRun p2 = p1.ok ? runFfmpeg(videoArgs(plan, state.fps, state.bitrateKbps, 2, attemptFile),
                                                   m_duration, mid, hi, prefix + QStringLiteral("第二遍高质量编码..."))
                                       : p1;
            ok = p2.ok;
            if (!ok)
                result.statusText = QStringLiteral("编码失败: ") + p2.error;
        } else {
            const FfmpegRun p = runFfmpeg(videoArgs(plan, state.fps, state.bitrateKbps, 0, attemptFile), m_duration, lo,
                                          hi, prefix + QStringLiteral("编码中..."));
            ok = p.ok;
            if (!ok)
                result.statusText = QStringLiteral("编码失败: ") + p.error;
        }
        if (cancelled())
            return result;
        if (!ok)
            return result;

        const qint64 size = QFileInfo(attemptFile).size();
        if (size <= 0) {
            result.statusText = QStringLiteral("编码失败: 输出文件为空");
            return result;
        }
        if (size <= plan.limitBytes) {
            bestFile = attemptFile;
            bestSize = size;
        } else if (state.raised && !bestFile.isEmpty()) {
            break; // 上调码率后超限：退回上一次合格的结果
        }

        const EncodePlan::Decision decision = EncodePlan::evaluate(plan, state, size);
        if (decision.verdict == EncodePlan::Verdict::Accept)
            break;
        if (decision.verdict == EncodePlan::Verdict::Fail) {
            if (!bestFile.isEmpty())
                break;
            result.statusText = decision.note;
            return result;
        }
        note = decision.note;
        report(50, note);
    }

    if (bestFile.isEmpty()) {
        result.statusText = QStringLiteral("无法将体积压缩至 %1KB 以内").arg(plan.limitBytes / 1024);
        return result;
    }

    report(97, QStringLiteral("写入文件..."));
    QFile::remove(m_req.outputFile);
    if (!QFile::copy(bestFile, m_req.outputFile)) {
        result.statusText = QStringLiteral("无法写入输出文件");
        return result;
    }
    // 3 秒破限：保留全部帧，只把文件头时长改写为 2.99 秒
    if (m_duration > spec::kOfficialMaxDuration) {
        QString error;
        if (!WebmDuration::write(m_req.outputFile, spec::kPatchedDuration, &error)) {
            result.statusText = QStringLiteral("改写时长失败: ") + error;
            return result;
        }
    }
    result.ok = true;
    result.size = QFileInfo(m_req.outputFile).size();
    result.statusText = QStringLiteral("转换完成，体积: %1 | %2").arg(kbText(result.size), summary);
    return result;
}

QStringList StickerEncoder::inputArgs(bool withMask) const
{
    const MediaInfo& info = m_req.task.info;
    QStringList args;
    if (m_start > kFastSeekThreshold)
        args << QStringLiteral("-ss") << seconds(m_start - kFastSeekLead);
    // FFmpeg 自带的 VP8/VP9 解码器会丢掉透明通道，必须用 libvpx 解码
    if (info.hasAlpha && info.videoCodec == u"vp9")
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9");
    else if (info.hasAlpha && info.videoCodec == u"vp8")
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx");
    args << QStringLiteral("-i") << native(m_req.task.inputPath);
    if (withMask)
        args << QStringLiteral("-i") << native(m_maskPath);
    return args;
}

QStringList StickerEncoder::videoArgs(const EncodePlan::Plan& plan, double fps, int bitrate, int pass,
                                      const QString& output) const
{
    const bool alpha = needsAlphaOutput(plan.content, plan.canvas);
    QStringList args = inputArgs(!m_maskPath.isEmpty());
    args << QStringLiteral("-filter_complex") << filterGraph(plan.content, plan.canvas, fps, false)
         << QStringLiteral("-map") << QStringLiteral("[out]");
    // 起点超过 10 秒：输入端已快速定位到起点前 4 秒，这里再精确定位剩下的 4 秒
    const double outputSeek = m_start > kFastSeekThreshold ? kFastSeekLead : m_start;
    if (outputSeek > 0)
        args << QStringLiteral("-ss") << seconds(outputSeek);
    args << QStringLiteral("-t") << seconds(m_duration);

    const int frames = int(std::ceil(m_duration * fps)) + 1;
    args << QStringLiteral("-an") << QStringLiteral("-sn") << QStringLiteral("-dn")
         << QStringLiteral("-map_metadata") << QStringLiteral("-1") << QStringLiteral("-map_chapters") << QStringLiteral("-1")
         << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9")
         << QStringLiteral("-pix_fmt") << (alpha ? QStringLiteral("yuva420p") : QStringLiteral("yuv420p"))
         << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(bitrate);
    if (plan.constantQuality)
        args << QStringLiteral("-crf") << QString::number(plan.crf);
    const bool fast = !plan.twoPass;
    args << QStringLiteral("-deadline") << QStringLiteral("good")
         << QStringLiteral("-cpu-used") << QString::number(pass == 1 ? 4 : (fast ? 3 : 1))
         << QStringLiteral("-row-mt") << QStringLiteral("1")
         << QStringLiteral("-auto-alt-ref") << QStringLiteral("1") << QStringLiteral("-lag-in-frames") << QStringLiteral("25")
         // 只在开头放一个关键帧
         << QStringLiteral("-g") << QString::number(frames) << QStringLiteral("-keyint_min") << QString::number(frames)
         << QStringLiteral("-colorspace") << QStringLiteral("bt709") << QStringLiteral("-color_primaries") << QStringLiteral("bt709")
         << QStringLiteral("-color_trc") << QStringLiteral("bt709") << QStringLiteral("-color_range") << QStringLiteral("tv");
    if (pass > 0) {
        args << QStringLiteral("-pass") << QString::number(pass)
             << QStringLiteral("-passlogfile") << native(QDir(m_req.workDir).filePath(QStringLiteral("pass")));
    }
    if (pass == 1)
        args << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
    else
        args << QStringLiteral("-f") << QStringLiteral("webm") << native(output);
    return args;
}

std::optional<double> StickerEncoder::measureMotion(double start, double duration)
{
    const double sampleRate = kMotionSamples / std::max(duration, 0.1);
    QStringList args;
    if (start > 0)
        args << QStringLiteral("-ss") << seconds(start);
    const MediaInfo& info = m_req.task.info;
    if (info.hasAlpha && info.videoCodec == u"vp9")
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9");
    args << QStringLiteral("-i") << native(m_req.task.inputPath) << QStringLiteral("-t") << seconds(duration)
         << QStringLiteral("-vf")
         << QStringLiteral("fps=%1,scale=%2:%2:flags=area,format=gray").arg(sampleRate, 0, 'f', 4).arg(kMotionSide)
         << QStringLiteral("-frames:v") << QString::number(kMotionSamples)
         << QStringLiteral("-an") << QStringLiteral("-f") << QStringLiteral("rawvideo") << QStringLiteral("-");
    const FfmpegRun run = runFfmpeg(args, 0, 2, 5, QStringLiteral("分析运动量..."), true);
    constexpr int frameBytes = kMotionSide * kMotionSide;
    const int frames = int(run.stdOut.size() / frameBytes);
    if (!run.ok || frames < 2)
        return std::nullopt;

    double total = 0;
    const auto* data = reinterpret_cast<const quint8*>(run.stdOut.constData());
    for (int f = 1; f < frames; ++f) {
        const quint8* a = data + qsizetype(f - 1) * frameBytes;
        const quint8* b = data + qsizetype(f) * frameBytes;
        qint64 diff = 0;
        for (int i = 0; i < frameBytes; ++i)
            diff += std::abs(int(a[i]) - int(b[i]));
        total += double(diff) / frameBytes / 255.0;
    }
    return total / (frames - 1);
}

// ---------------------------------------------------------------- 静态图

EncodeResult StickerEncoder::encodeStatic()
{
    EncodeResult result;
    const QSize content = EncodePlan::staticContentSize(sourceSize(), m_req.mode);
    const QSize canvas = EncodePlan::canvasSize(content, m_req.mode);
    const qint64 limit = EncodePlan::staticLimitBytes(m_req.mode);
    const QString failText = QStringLiteral("静态转换失败或体积超出 %1KB").arg(limit / 1024);

    // TODO(解析素材): HEIC/HEIF 按 EXIF 方向摆正后再处理
    const QString frameFile = QDir(m_req.workDir).filePath(QStringLiteral("frame.png"));
    QStringList args = inputArgs(!m_maskPath.isEmpty());
    args << QStringLiteral("-filter_complex") << filterGraph(content, canvas, 0, true)
         << QStringLiteral("-map") << QStringLiteral("[out]") << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-map_metadata") << QStringLiteral("-1")
         << QStringLiteral("-c:v") << QStringLiteral("png") << QStringLiteral("-f") << QStringLiteral("image2")
         << native(frameFile);
    const FfmpegRun render = runFfmpeg(args, 0, 5, 30, QStringLiteral("处理画面..."));
    if (cancelled())
        return result;
    if (!render.ok) {
        result.statusText = QStringLiteral("处理画面失败: ") + render.error;
        return result;
    }
    const QImage image(frameFile);
    if (image.isNull()) {
        result.statusText = QStringLiteral("无法读取处理后的画面");
        return result;
    }

    struct Step {
        WebpEncoder::Mode mode;
        int quality;
        QString label;
    };
    QList<Step> steps{{WebpEncoder::Mode::Lossless, 100, QStringLiteral("纯无损")},
                      {WebpEncoder::Mode::NearLossless, 100, QStringLiteral("近无损")}};
    for (const int q : {98, 95, 90, 85, 80, 70, 60})
        steps << Step{WebpEncoder::Mode::Lossy, q, QStringLiteral("有损 q%1").arg(q)};

    for (int i = 0; i < steps.size(); ++i) {
        if (cancelled())
            return result;
        const Step& step = steps.at(i);
        report(35 + i * 60 / int(steps.size()), QStringLiteral("WebP 编码（%1）...").arg(step.label));
        QString error;
        const QByteArray data = WebpEncoder::encode(image, step.mode, step.quality, &error);
        if (data.isEmpty()) {
            result.statusText = error.isEmpty() ? failText : error;
            return result;
        }
        if (data.size() > limit)
            continue;
        QFile out(m_req.outputFile);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
            result.statusText = QStringLiteral("无法写入输出文件");
            return result;
        }
        out.close();
        result.ok = true;
        result.size = data.size();
        result.statusText = QStringLiteral("转换完成（%1），体积: %2").arg(step.label, kbText(result.size));
        return result;
    }
    result.statusText = failText;
    return result;
}

// ---------------------------------------------------------------- 公共

QSize StickerEncoder::sourceSize() const
{
    const MediaInfo& info = m_req.task.info;
    const QSize frame(info.width, info.height);
    if (m_req.task.crop)
        return FilterGraph::evenCrop(*m_req.task.crop, frame).size();
    return frame;
}

bool StickerEncoder::needsAlphaOutput(QSize content, QSize canvas) const
{
    return m_req.task.info.hasAlpha || m_req.task.cropRadius > 0 || content != canvas;
}

QString StickerEncoder::filterGraph(QSize scale, QSize canvas, double fps, bool rgba) const
{
    const StickerTask& t = m_req.task;
    FilterGraph::Spec spec;
    spec.mirror = t.mirror;
    if (t.crop)
        spec.crop = FilterGraph::evenCrop(*t.crop, QSize(t.info.width, t.info.height));
    spec.scale = scale;
    spec.preset = m_req.preset;
    spec.fps = fps;
    spec.roundMask = !m_maskPath.isEmpty();
    spec.sourceAlpha = t.info.hasAlpha;
    spec.alphaOutput = needsAlphaOutput(scale, canvas);
    spec.canvas = canvas;
    spec.rgbaOutput = rgba;
    return FilterGraph::build(spec);
}

bool StickerEncoder::writeMask(QSize size)
{
    m_maskPath = QDir(m_req.workDir).filePath(QStringLiteral("mask.png"));
    return FilterGraph::writeRoundMask(size, m_req.task.cropRadius, m_maskPath);
}

void StickerEncoder::report(int percent, const QString& text)
{
    if (m_progress)
        m_progress(std::clamp(percent, 0, 100), text);
}

StickerEncoder::FfmpegRun StickerEncoder::runFfmpeg(const QStringList& args, double duration, int from, int to,
                                                    const QString& text, bool captureStdout)
{
    FfmpegRun run;
    QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                     QStringLiteral("-loglevel"), QStringLiteral("error")};
    if (!captureStdout)
        full << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats");
    full << args;

    report(from, text);
    QProcess process;
    ProcessRunner::hideConsoleWindow(process);
    process.setWorkingDirectory(m_req.workDir);
    process.start(FFmpegTools::ffmpegPath(), full);
    if (!process.waitForStarted(10000)) {
        run.error = process.errorString();
        return run;
    }

    QByteArray pending;
    int lastPercent = from;
    while (process.state() != QProcess::NotRunning) {
        if (cancelled()) {
            process.kill();
            process.waitForFinished(3000);
            run.cancelled = true;
            return run;
        }
        process.waitForReadyRead(100);
        const QByteArray chunk = process.readAllStandardOutput();
        if (captureStdout) {
            run.stdOut += chunk;
            continue;
        }
        pending += chunk;
        qsizetype nl;
        while ((nl = pending.indexOf('\n')) >= 0) {
            const QByteArray line = pending.left(nl).trimmed();
            pending.remove(0, nl + 1);
            if (duration > 0 && line.startsWith("out_time_us=")) {
                const double t = line.mid(12).toDouble() / 1e6;
                const int percent = from + int(std::clamp(t / duration, 0.0, 1.0) * (to - from));
                if (percent != lastPercent) {
                    lastPercent = percent;
                    report(percent, text);
                }
            }
        }
    }
    if (captureStdout)
        run.stdOut += process.readAllStandardOutput();

    const QByteArray err = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QStringList lines = QString::fromUtf8(err).split(u'\n', Qt::SkipEmptyParts);
        run.error = lines.isEmpty() ? QStringLiteral("ffmpeg 退出码 %1").arg(process.exitCode()) : lines.last().trimmed();
        return run;
    }
    run.ok = true;
    report(to, text);
    return run;
}

} // namespace tsm
