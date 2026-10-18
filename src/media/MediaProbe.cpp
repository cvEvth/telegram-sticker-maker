#include "media/MediaProbe.h"

#include "core/Format.h"
#include "core/ImageHeaders.h"
#include "core/MediaKinds.h"
#include "media/FFmpegTools.h"
#include "media/ProcessRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <optional>

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

// 旋转角度：视频流的显示矩阵，或首帧的显示矩阵（JPEG / HEIC 的方向信息在帧上）
int rotationOf(const QJsonObject& stream, const QJsonArray& frames)
{
    const auto fromList = [](const QJsonArray& list) -> std::optional<int> {
        for (const QJsonValue& v : list) {
            const QJsonObject o = v.toObject();
            if (o.contains(u"rotation"))
                return int(std::lround(o.value(u"rotation").toDouble()));
        }
        return std::nullopt;
    };
    if (const auto r = fromList(stream.value(u"side_data_list").toArray()))
        return *r;
    const int index = stream.value(u"index").toInt();
    for (const QJsonValue& v : frames) {
        const QJsonObject frame = v.toObject();
        if (frame.value(u"media_type").toString() != u"video" || frame.value(u"stream_index").toInt() != index)
            continue;
        if (const auto r = fromList(frame.value(u"side_data_list").toArray()))
            return *r;
    }
    return 0;
}

// FFmpeg 不能解码动态 WebP：直接读文件头（解码时走 SourceInput 的中间文件）
ProbeResult probeAnimatedWebp(const QString& path, MediaInfo info)
{
    ProbeResult result;
    const ImageHeaders::Info header = ImageHeaders::scanFile(path);
    if (!header.parsed || !header.animated) {
        result.error = QCoreApplication::translate("MediaProbe", "无法解析动态 WebP");
        return result;
    }
    info.isVideo = true;
    info.isAnimatedImage = true;
    info.width = header.width;
    info.height = header.height;
    info.hasAlpha = header.hasAlpha;
    info.duration = header.durationSeconds();
    info.fps = normalizeFrameRate(header.fps());
    info.container = QStringLiteral("webp");
    info.videoCodec = QStringLiteral("webp");
    result.info = info;
    result.ok = info.isValid();
    if (!result.ok)
        result.error = QCoreApplication::translate("MediaProbe", "无法读取画面尺寸");
    return result;
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
        result.error = QCoreApplication::translate("MediaProbe", "文件不存在");
        return result;
    }
    const MediaKind kind = MediaKinds::classify(path);
    if (kind == MediaKind::Unsupported) {
        result.error = QCoreApplication::translate("MediaProbe", "不支持的文件格式");
        return result;
    }
    const bool webp = fileInfo.suffix().compare(u"webp", Qt::CaseInsensitive) == 0;
    if (kind == MediaKind::AnimatedImage && webp)
        return probeAnimatedWebp(path, info);

    const QString ffprobe = FFmpegTools::ffprobePath();
    if (ffprobe.isEmpty()) {
        result.error = QCoreApplication::translate("MediaProbe", "找不到 ffprobe");
        return result;
    }

    const ProcessResult run = ProcessRunner::run(
        ffprobe,
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
         QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
         // 首帧的附加信息里有 JPEG / HEIC 的方向
         QStringLiteral("-show_frames"), QStringLiteral("-read_intervals"), QStringLiteral("%+#1"),
         QDir::toNativeSeparators(fileInfo.absoluteFilePath())},
        30000);
    if (!run.ok()) {
        result.error = run.errorSummary();
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(run.stdOut, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QCoreApplication::translate("MediaProbe", "无法解析 ffprobe 输出");
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
        result.error = QCoreApplication::translate("MediaProbe", "未找到视频或图像流");
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

    // 旋转 90° / 270° 时 ffmpeg 解码会自动摆正，宽高按摆正后的画面记录（HEIC / JPEG 方向、手机竖拍视频）
    const int rotation = ((rotationOf(video, root.value(u"frames").toArray()) % 360) + 360) % 360;
    if (rotation == 90 || rotation == 270)
        std::swap(info.width, info.height);

    // GIF / PNG / WebP 的透明与动图时长以文件头为准：FFmpeg 的 GIF 解码器总是输出 BGRA，
    // 调色板 PNG 的 tRNS 透明也看不出来
    if (const ImageHeaders::Info header = ImageHeaders::scanFile(path); header.parsed) {
        info.hasAlpha = header.hasAlpha;
        if (info.isAnimatedImage && header.animated && header.durationMs > 0) {
            info.duration = header.durationSeconds();
            info.fps = normalizeFrameRate(header.fps());
        }
    }

    if (!info.isValid()) {
        result.error = QCoreApplication::translate("MediaProbe", "无法读取画面尺寸");
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace tsm
