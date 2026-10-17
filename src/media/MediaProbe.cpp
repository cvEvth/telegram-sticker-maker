#include "media/MediaProbe.h"

#include "core/Format.h"
#include "core/MediaKinds.h"
#include "media/FFmpegTools.h"
#include "media/ProcessRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tsm {

namespace {
double parseRational(const QString& text)
{
    const QStringList parts = text.split(u'/');
    if (parts.size() == 2) {
        const double den = parts[1].toDouble();
        return den > 0 ? parts[0].toDouble() / den : 0.0;
    }
    return text.toDouble();
}

bool pixelFormatHasAlpha(const QString& f)
{
    return f.startsWith(u"yuva") || f.startsWith(u"gbrap") || f.startsWith(u"ya") || f.contains(u"rgba")
        || f.contains(u"bgra") || f.contains(u"argb") || f.contains(u"abgr");
}

// WebM 里的 VP9 透明视频把透明信息放在流标签 alpha_mode=1
bool tagsSayAlpha(const QJsonObject& tags)
{
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        if (it.key().compare(u"alpha_mode", Qt::CaseInsensitive) == 0 && it.value().toString().trimmed() == u"1")
            return true;
    }
    return false;
}

bool usableFps(double fps)
{
    return fps > 0 && fps <= 240;
}
} // namespace

ProbeResult MediaProbe::probe(const QString& path)
{
    ProbeResult result;
    MediaInfo& info = result.info;
    const QFileInfo fileInfo(path);
    info.fileName = fileInfo.fileName();
    info.fileSize = fileInfo.size();

    if (!fileInfo.isFile()) {
        result.error = QStringLiteral("文件不存在");
        return result;
    }
    const MediaKind kind = MediaKinds::classify(path);
    if (kind == MediaKind::Unsupported) {
        result.error = QStringLiteral("不支持的文件格式");
        return result;
    }
    const QString ffprobe = FFmpegTools::ffprobePath();
    if (ffprobe.isEmpty()) {
        result.error = QStringLiteral("找不到 ffprobe");
        return result;
    }

    const ProcessResult run = ProcessRunner::run(
        ffprobe,
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
         QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
         QDir::toNativeSeparators(fileInfo.absoluteFilePath())},
        30000);
    if (!run.ok()) {
        result.error = run.errorSummary();
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(run.stdOut, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("无法解析 ffprobe 输出");
        return result;
    }
    const QJsonObject root = doc.object();

    QJsonObject video;
    for (const QJsonValue& value : root.value(u"streams").toArray()) {
        const QJsonObject stream = value.toObject();
        if (stream.value(u"codec_type").toString() != u"video")
            continue;
        // 视频里的封面图不算画面流
        if (kind == MediaKind::Video && stream.value(u"disposition").toObject().value(u"attached_pic").toInt() == 1)
            continue;
        video = stream;
        break;
    }
    if (video.isEmpty()) {
        result.error = QStringLiteral("未找到视频或图像流");
        return result;
    }

    const QJsonObject format = root.value(u"format").toObject();
    info.isVideo = kind != MediaKind::StaticImage;
    info.isAnimatedImage = kind == MediaKind::AnimatedImage;
    info.width = video.value(u"width").toInt();
    info.height = video.value(u"height").toInt();
    info.videoCodec = video.value(u"codec_name").toString();
    info.container = format.value(u"format_name").toString();
    info.hasAlpha = pixelFormatHasAlpha(video.value(u"pix_fmt").toString()) || tagsSayAlpha(video.value(u"tags").toObject());

    if (info.isVideo) {
        double duration = format.value(u"duration").toString().toDouble();
        if (duration <= 0)
            duration = video.value(u"duration").toString().toDouble();
        info.duration = qMax(0.0, duration);

        double fps = parseRational(video.value(u"avg_frame_rate").toString());
        if (!usableFps(fps))
            fps = parseRational(video.value(u"r_frame_rate").toString());
        const int frames = video.value(u"nb_frames").toString().toInt();
        if (!usableFps(fps) && frames > 0 && info.duration > 0)
            fps = frames / info.duration;
        info.fps = normalizeFrameRate(fps);
    }
    // TODO(解析素材): 动图时长按逐帧时长累加（单帧缺省 100ms）；HEIC/HEIF 按 EXIF 方向摆正宽高；
    //                 GIF / PNG 调色板透明精确检测（目前按像素格式判断）

    if (!info.isValid()) {
        result.error = QStringLiteral("无法读取画面尺寸");
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace tsm
