#include "media/SourceTrimmer.h"

#include "media/FfmpegRun.h"
#include "media/FilterGraph.h"
#include "media/WebpEncoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>

namespace tsm::SourceTrimmer {

namespace {
QString seconds(double v)
{
    return QString::number(v, 'f', 3);
}

QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

bool hasTransform(const StickerTask& t)
{
    return t.crop.has_value() || t.cropRadius > 0 || t.mirror;
}

QSize frameSize(const StickerTask& t)
{
    return QSize(t.info.width, t.info.height);
}

std::optional<QRect> evenCrop(const StickerTask& t)
{
    if (!t.crop)
        return std::nullopt;
    return FilterGraph::evenCrop(*t.crop, frameSize(t));
}

QStringList decoderArgs(const StickerTask& t)
{
    // FFmpeg 自带的 VP8/VP9 解码器会丢掉透明通道
    if (t.info.hasAlpha && t.info.videoCodec == u"vp9")
        return {QStringLiteral("-c:v"), QStringLiteral("libvpx-vp9")};
    if (t.info.hasAlpha && t.info.videoCodec == u"vp8")
        return {QStringLiteral("-c:v"), QStringLiteral("libvpx")};
    return {};
}

Result copyWhole(const StickerTask& task, const QString& extension, const QString& note)
{
    Result r;
    r.ok = true;
    r.file = task.inputPath;
    r.extension = extension;
    r.isSourceCopy = true;
    r.note = note;
    return r;
}

Result streamCut(const StickerTask& task, const QString& outputBase, const QString& extension,
                 const std::shared_ptr<std::atomic_bool>& cancel, const Progress& progress)
{
    const double start = task.startTime.value_or(0.0);
    const double duration = task.effectiveDuration();
    const QString output = outputBase + u'.' + extension;
    const auto attempt = [&](const QStringList& maps, int from, int to) {
        QStringList args{QStringLiteral("-ss"), seconds(start), QStringLiteral("-i"), native(task.inputPath),
                         QStringLiteral("-t"), seconds(duration)};
        args << maps << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero") << native(output);
        return FfmpegRun::run(args, duration, cancel, [&](double f) {
            if (progress)
                progress(from + int(f * (to - from)), QStringLiteral("直接剪切..."));
        });
    };

    if (progress)
        progress(5, QStringLiteral("直接剪切..."));
    FfmpegRun::Result run = attempt({QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a?")}, 5, 95);
    if (run.cancelled)
        return Result{false, true};
    if (!run.ok || QFileInfo(output).size() == 0) {
        run = attempt({QStringLiteral("-map"), QStringLiteral("0")}, 5, 95);
        if (run.cancelled)
            return Result{false, true};
    }
    if (run.ok && QFileInfo(output).size() > 0) {
        Result r;
        r.ok = true;
        r.file = output;
        r.extension = extension;
        r.note = QStringLiteral("已导出（直接剪切）");
        return r;
    }
    QFile::remove(output);
    return copyWhole(task, extension, QStringLiteral("已导出（无法剪切，已复制整个文件）"));
}

Result reencodeVideo(const StickerTask& task, const QString& workDir, const QString& outputBase,
                     const std::shared_ptr<std::atomic_bool>& cancel, const Progress& progress)
{
    Result r;
    const double start = task.startTime.value_or(0.0);
    const double duration = task.effectiveDuration();
    const std::optional<QRect> crop = evenCrop(task);
    const QSize content = crop ? crop->size() : QSize(frameSize(task).width() & ~1, frameSize(task).height() & ~1);
    const bool alpha = task.info.hasAlpha || task.cropRadius > 0;

    QString mask;
    if (task.cropRadius > 0) {
        mask = QDir(workDir).filePath(QStringLiteral("mask.png"));
        if (!FilterGraph::writeRoundMask(content, task.cropRadius, mask)) {
            r.error = QStringLiteral("无法生成圆角遮罩");
            return r;
        }
    }

    FilterGraph::Spec spec;
    spec.mirror = task.mirror;
    spec.crop = crop;
    spec.scale = content;
    spec.preset = EncodePreset::Fast; // 不加锐化 / 降噪，保持原画
    spec.roundMask = !mask.isEmpty();
    spec.sourceAlpha = task.info.hasAlpha;
    spec.alphaOutput = alpha;
    spec.canvas = content;

    QStringList args;
    if (start > 0)
        args << QStringLiteral("-ss") << seconds(start);
    args << decoderArgs(task) << QStringLiteral("-i") << native(task.inputPath);
    if (!mask.isEmpty())
        args << QStringLiteral("-i") << native(mask);
    args << QStringLiteral("-filter_complex") << FilterGraph::build(spec) << QStringLiteral("-map") << QStringLiteral("[out]")
         << QStringLiteral("-map") << QStringLiteral("0:a:0?") << QStringLiteral("-t") << seconds(duration)
         << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9") << QStringLiteral("-crf") << QStringLiteral("18")
         << QStringLiteral("-b:v") << QStringLiteral("0") << QStringLiteral("-deadline") << QStringLiteral("good")
         << QStringLiteral("-cpu-used") << QStringLiteral("2") << QStringLiteral("-row-mt") << QStringLiteral("1")
         << QStringLiteral("-pix_fmt") << (alpha ? QStringLiteral("yuva420p") : QStringLiteral("yuv420p"))
         << QStringLiteral("-c:a") << QStringLiteral("libopus") << QStringLiteral("-b:a") << QStringLiteral("128k")
         << QStringLiteral("-f") << QStringLiteral("webm");
    const QString output = outputBase + QStringLiteral(".webm");
    args << native(output);

    if (progress)
        progress(5, QStringLiteral("高画质编码..."));
    const FfmpegRun::Result run = FfmpegRun::run(args, duration, cancel, [&](double f) {
        if (progress)
            progress(5 + int(f * 90), QStringLiteral("高画质编码..."));
    });
    if (run.cancelled) {
        r.cancelled = true;
        return r;
    }
    if (!run.ok) {
        QFile::remove(output);
        r.error = QStringLiteral("编码失败: ") + run.error;
        return r;
    }
    r.ok = true;
    r.file = output;
    r.extension = QStringLiteral("webm");
    r.note = QStringLiteral("已导出（高画质 WebM）");
    return r;
}

Result renderImage(const StickerTask& task, const QString& workDir, const QString& outputBase, const QString& extension,
                   const std::shared_ptr<std::atomic_bool>& cancel, const Progress& progress)
{
    Result r;
    const std::optional<QRect> crop = evenCrop(task);
    const QSize content = crop ? crop->size() : frameSize(task);

    QString mask;
    if (task.cropRadius > 0) {
        mask = QDir(workDir).filePath(QStringLiteral("mask.png"));
        if (!FilterGraph::writeRoundMask(content, task.cropRadius, mask)) {
            r.error = QStringLiteral("无法生成圆角遮罩");
            return r;
        }
    }
    FilterGraph::Spec spec;
    spec.mirror = task.mirror;
    spec.crop = crop;
    spec.scale = content;
    spec.preset = EncodePreset::Fast;
    spec.roundMask = !mask.isEmpty();
    spec.sourceAlpha = task.info.hasAlpha;
    spec.canvas = content;
    spec.rgbaOutput = true;

    const QString frame = QDir(workDir).filePath(QStringLiteral("frame.png"));
    QStringList args;
    args << decoderArgs(task) << QStringLiteral("-i") << native(task.inputPath);
    if (!mask.isEmpty())
        args << QStringLiteral("-i") << native(mask);
    args << QStringLiteral("-filter_complex") << FilterGraph::build(spec) << QStringLiteral("-map") << QStringLiteral("[out]")
         << QStringLiteral("-frames:v") << QStringLiteral("1") << QStringLiteral("-c:v") << QStringLiteral("png")
         << QStringLiteral("-f") << QStringLiteral("image2") << native(frame);
    if (progress)
        progress(10, QStringLiteral("处理画面..."));
    const FfmpegRun::Result run = FfmpegRun::run(args, 0, cancel);
    if (run.cancelled) {
        r.cancelled = true;
        return r;
    }
    if (!run.ok) {
        r.error = QStringLiteral("处理画面失败: ") + run.error;
        return r;
    }

    const QString output = outputBase + u'.' + extension;
    QFile::remove(output);
    bool saved = false;
    if (extension == u"png") {
        saved = QFile::rename(frame, output);
    } else {
        const QImage image(frame);
        if (image.isNull()) {
            r.error = QStringLiteral("无法读取处理后的画面");
            return r;
        }
        if (extension == u"webp") {
            const QByteArray data = WebpEncoder::encode(image, WebpEncoder::Mode::Lossless, 100, &r.error);
            QFile out(output);
            saved = !data.isEmpty() && out.open(QIODevice::WriteOnly) && out.write(data) == data.size();
        } else {
            // JPEG 没有透明通道，铺白底
            QImage flat(image.size(), QImage::Format_RGB32);
            flat.fill(Qt::white);
            QPainter(&flat).drawImage(0, 0, image);
            saved = flat.save(output, "JPG", 95);
        }
    }
    if (!saved) {
        if (r.error.isEmpty())
            r.error = QStringLiteral("无法写入输出文件");
        return r;
    }
    r.ok = true;
    r.file = output;
    r.extension = extension;
    r.note = QStringLiteral("已导出（已裁切图片）");
    if (progress)
        progress(100, QStringLiteral("处理画面..."));
    return r;
}
} // namespace

Decision decide(const StickerTask& task)
{
    const QString ext = QFileInfo(task.inputPath).suffix().toLower();
    if (task.info.isVideo) {
        if (hasTransform(task))
            return {Method::Reencode, QStringLiteral("webm")};
        if (!task.isTrimmed())
            return {Method::CopyWhole, ext};
        return {Method::StreamCut, ext};
    }
    if (task.cropRadius > 0)
        return {Method::RenderImage, QStringLiteral("png")};
    if (!task.crop && !task.mirror)
        return {Method::CopyWhole, ext};
    static const QStringList keep{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp")};
    return {Method::RenderImage, keep.contains(ext) ? ext : QStringLiteral("png")};
}

Result run(const StickerTask& task, const QString& workDir, const QString& outputBase,
           const std::shared_ptr<std::atomic_bool>& cancel, const Progress& progress)
{
    if (!QFileInfo(task.inputPath).isFile()) {
        Result r;
        r.error = QStringLiteral("源文件缺失");
        return r;
    }
    QDir().mkpath(workDir);
    QDir().mkpath(QFileInfo(outputBase).absolutePath());
    const Decision d = decide(task);
    Result r;
    switch (d.method) {
    case Method::CopyWhole:
        r = copyWhole(task, d.extension, task.info.isVideo ? QStringLiteral("已导出（整段复制）") : QStringLiteral("已导出（原图复制）"));
        break;
    case Method::StreamCut:
        r = streamCut(task, outputBase, d.extension, cancel, progress);
        break;
    case Method::Reencode:
        r = reencodeVideo(task, workDir, outputBase, cancel, progress);
        break;
    case Method::RenderImage:
        r = renderImage(task, workDir, outputBase, d.extension, cancel, progress);
        break;
    }
    if (cancel && cancel->load()) {
        r.ok = false;
        r.cancelled = true;
    }
    return r;
}

} // namespace tsm::SourceTrimmer
