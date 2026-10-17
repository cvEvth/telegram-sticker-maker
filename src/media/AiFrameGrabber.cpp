#include "media/AiFrameGrabber.h"

#include "core/AiEmoji.h"
#include "media/FfmpegRun.h"
#include "media/FilterGraph.h"
#include "media/SourceInput.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPainter>
#include <cmath>

namespace tsm::AiFrameGrabber {

namespace {
QString seconds(double v)
{
    return QString::number(v, 'f', 3);
}

QImage onWhite(const QImage& image)
{
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter(&flat).drawImage(0, 0, image);
    return flat;
}

// 内容尺寸按比例缩到 maxW × maxH 以内（不放大）
QSize fitWithin(QSize size, int maxW, int maxH)
{
    if (size.width() <= maxW && size.height() <= maxH)
        return size;
    QSize fitted = size.scaled(maxW, maxH, Qt::KeepAspectRatio);
    return fitted.expandedTo(QSize(1, 1));
}
} // namespace

QImage composeStrip(const QList<QImage>& frames)
{
    if (frames.isEmpty())
        return {};
    if (frames.size() == 1)
        return frames.first();
    int width = kGap * int(frames.size() - 1);
    int height = 0;
    for (const QImage& f : frames) {
        width += f.width();
        height = std::max(height, f.height());
    }
    QImage strip(width, height, QImage::Format_RGB32);
    strip.fill(QColor(0x80, 0x80, 0x80));
    QPainter painter(&strip);
    int x = 0;
    for (const QImage& f : frames) {
        painter.drawImage(x, (height - f.height()) / 2, f);
        x += f.width() + kGap;
    }
    painter.end();
    if (strip.width() > kStripMaxWidth)
        strip = strip.scaledToWidth(kStripMaxWidth, Qt::SmoothTransformation);
    return strip;
}

Result grab(const StickerTask& task, const QString& workDir, const std::shared_ptr<std::atomic_bool>& cancel)
{
    Result result;
    if (!QFileInfo::exists(task.inputPath)) {
        result.error = QStringLiteral("源文件不存在");
        return result;
    }
    QDir().mkpath(workDir);
    const QSize frame(task.info.width, task.info.height);
    std::optional<QRect> crop;
    if (task.crop)
        crop = FilterGraph::evenCrop(*task.crop, frame);
    const QSize content = crop ? crop->size() : frame;
    if (!content.isValid()) {
        result.error = QStringLiteral("无法获取画面尺寸");
        return result;
    }

    const bool video = task.info.isVideo;
    QSize scale = video ? fitWithin(content, kStripMaxWidth, kFrameHeight) : fitWithin(content, kStaticMaxSide, kStaticMaxSide);
    QString mask;
    if (task.cropRadius > 0) {
        mask = QDir(workDir).filePath(QStringLiteral("ai_mask.png"));
        if (!FilterGraph::writeRoundMask(scale, task.cropRadius, mask)) {
            result.error = QStringLiteral("无法生成圆角遮罩");
            return result;
        }
    }

    FilterGraph::Spec spec;
    spec.mirror = task.mirror;
    spec.crop = crop;
    spec.scale = scale;
    spec.preset = EncodePreset::Fast;
    spec.roundMask = !mask.isEmpty();
    spec.sourceAlpha = task.info.hasAlpha;
    spec.canvas = scale;
    spec.rgbaOutput = true;
    const QString graph = FilterGraph::build(spec);

    QList<double> times{0.0};
    if (video) {
        const double start = task.startTime.value_or(0.0);
        times.clear();
        for (const double t : AiEmoji::sampleTimes(task.effectiveDuration()))
            times << start + t;
    }

    QList<QImage> frames;
    for (int i = 0; i < times.size(); ++i) {
        const QString out = QDir(workDir).filePath(QStringLiteral("ai_frame_%1.png").arg(i));
        QStringList args;
        if (video && times.at(i) > 0)
            args << QStringLiteral("-ss") << seconds(times.at(i));
        if (task.info.hasAlpha && task.info.videoCodec == u"vp9")
            args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9");
        else if (task.info.hasAlpha && task.info.videoCodec == u"vp8")
            args << QStringLiteral("-c:v") << QStringLiteral("libvpx");
        args << QStringLiteral("-i") << QDir::toNativeSeparators(SourceInput::resolve(task.inputPath));
        if (!mask.isEmpty())
            args << QStringLiteral("-i") << QDir::toNativeSeparators(mask);
        args << QStringLiteral("-filter_complex") << graph << QStringLiteral("-map") << QStringLiteral("[out]")
             << QStringLiteral("-frames:v") << QStringLiteral("1") << QStringLiteral("-c:v") << QStringLiteral("png")
             << QStringLiteral("-f") << QStringLiteral("image2") << QDir::toNativeSeparators(out);
        const FfmpegRun::Result run = FfmpegRun::run(args, 0, cancel);
        if (run.cancelled) {
            result.error = QStringLiteral("已取消");
            return result;
        }
        const QImage image(out);
        QFile::remove(out);
        if (!run.ok || image.isNull()) {
            // 末尾时刻偶尔取不到帧（片段贴着结尾），跳过即可
            if (!frames.isEmpty() || i + 1 < times.size())
                continue;
            result.error = run.ok ? QStringLiteral("无法读取画面") : QStringLiteral("取帧失败: ") + run.error;
            return result;
        }
        frames << onWhite(image);
    }
    if (frames.isEmpty()) {
        result.error = QStringLiteral("无法读取画面");
        return result;
    }

    result.image = composeStrip(frames);
    QBuffer buffer(&result.jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!result.image.save(&buffer, "JPG", kJpegQuality)) {
        result.error = QStringLiteral("无法编码 JPEG");
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace tsm::AiFrameGrabber
