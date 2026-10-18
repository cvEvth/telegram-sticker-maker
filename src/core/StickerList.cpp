#include "core/StickerList.h"

#include "core/Keywords.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace tsm::StickerList {

namespace {
// 时间保留到毫秒，避免 1.2000000000000002 这类写法
double roundMs(double seconds)
{
    return std::round(seconds * 1000.0) / 1000.0;
}

QJsonObject mediaObject(const StickerTask& t)
{
    const MediaInfo& m = t.info;
    const QFileInfo fi(t.inputPath);
    return QJsonObject{
        {QStringLiteral("width"), m.width},
        {QStringLiteral("height"), m.height},
        {QStringLiteral("duration"), m.duration},
        {QStringLiteral("fps"), m.fps},
        {QStringLiteral("has_alpha"), m.hasAlpha},
        {QStringLiteral("is_animated"), m.isAnimatedImage},
        {QStringLiteral("container"), m.container},
        {QStringLiteral("video_codec"), m.videoCodec},
        {QStringLiteral("file_size"), double(fi.size())},
        {QStringLiteral("mtime"), double(fi.lastModified().toMSecsSinceEpoch())},
    };
}

void readMedia(const QJsonObject& obj, Entry& e)
{
    MediaInfo& m = e.task.info;
    m.width = obj.value(u"width").toInt();
    m.height = obj.value(u"height").toInt();
    m.duration = obj.value(u"duration").toDouble();
    m.fps = obj.value(u"fps").toDouble();
    m.hasAlpha = obj.value(u"has_alpha").toBool();
    m.isAnimatedImage = obj.value(u"is_animated").toBool();
    m.container = obj.value(u"container").toString();
    m.videoCodec = obj.value(u"video_codec").toString();
    e.mediaFileSize = qint64(obj.value(u"file_size").toDouble());
    e.mediaMtime = qint64(obj.value(u"mtime").toDouble());
    e.hasMedia = m.width > 0 && m.height > 0 && e.mediaFileSize > 0;
}

QStringList readKeywords(const QJsonValue& v)
{
    if (v.isArray()) {
        QStringList list;
        for (const QJsonValue& item : v.toArray())
            if (const QString s = item.toString().trimmed(); !s.isEmpty())
                list << s;
        return list;
    }
    return Keywords::split(v.toString());
}

std::optional<QRect> readCrop(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 4)
        return std::nullopt;
    const QRect r(a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(), a.at(3).toInt());
    return r.width() > 0 && r.height() > 0 ? std::optional<QRect>(r) : std::nullopt;
}
} // namespace

QByteArray write(const QList<StickerTask>& tasks, const WriteOptions& options)
{
    QJsonArray stickers;
    for (const StickerTask& t : tasks) {
        QJsonObject o;
        o.insert(QStringLiteral("input_path"), options.pathFor ? options.pathFor(t) : QFileInfo(t.inputPath).absoluteFilePath());
        o.insert(QStringLiteral("file_name"), t.info.fileName);
        o.insert(QStringLiteral("is_video"), t.info.isVideo);
        if (!t.emoji.isEmpty())
            o.insert(QStringLiteral("emoji"), t.emoji);
        if (!t.keywords.isEmpty())
            o.insert(QStringLiteral("keywords"), t.keywords.join(QStringLiteral(", ")));
        if (t.startTime)
            o.insert(QStringLiteral("start_time"), roundMs(*t.startTime));
        if (t.endTime)
            o.insert(QStringLiteral("end_time"), roundMs(*t.endTime));
        if (t.crop)
            o.insert(QStringLiteral("crop"), QJsonArray{t.crop->x(), t.crop->y(), t.crop->width(), t.crop->height()});
        if (t.cropRadius > 0)
            o.insert(QStringLiteral("crop_radius"), std::round(t.cropRadius * 100) / 100);
        if (t.mirror)
            o.insert(QStringLiteral("mirror"), true);
        if (options.proxyFor) {
            if (const QString proxy = options.proxyFor(t); !proxy.isEmpty())
                o.insert(QStringLiteral("proxy"), proxy);
        }
        if (!t.clipGroupId.isEmpty()) {
            o.insert(QStringLiteral("clip_group_id"), t.clipGroupId);
            o.insert(QStringLiteral("clip_label"), t.clipLabel());
        }
        if (!t.clipId.isEmpty())
            o.insert(QStringLiteral("clip_id"), t.clipId);
        if (options.includeMedia && t.info.isValid())
            o.insert(QStringLiteral("media"), mediaObject(t));
        stickers.append(o);
    }

    QJsonObject root{
        {QStringLiteral("kind"), QString::fromLatin1(kKind)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("export_mode"), options.exportMode},
    };
    if (options.exportedAt.isValid())
        root.insert(QStringLiteral("exported_at"), options.exportedAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("stickers"), stickers);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ParseResult parse(const QByteArray& json)
{
    ParseResult result;
    result.error = QCoreApplication::translate("StickerList", "列表文件格式无效");
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return result;
    const QJsonObject root = doc.object();
    const QJsonValue stickers = root.value(u"stickers");
    if (!stickers.isArray())
        return result;

    if (root.value(u"kind").toString() == QLatin1StringView(kKind)) {
        result.kind = Kind::StickerList;
        for (const QJsonValue& v : stickers.toArray()) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.task.inputPath = o.value(u"input_path").toString();
            if (e.task.inputPath.isEmpty())
                continue;
            e.task.info.fileName = o.value(u"file_name").toString(QFileInfo(e.task.inputPath).fileName());
            e.task.info.isVideo = o.value(u"is_video").toBool();
            e.task.emoji = o.value(u"emoji").toString();
            e.task.keywords = readKeywords(o.value(u"keywords"));
            if (o.contains(u"start_time"))
                e.task.startTime = o.value(u"start_time").toDouble();
            if (o.contains(u"end_time"))
                e.task.endTime = o.value(u"end_time").toDouble();
            e.task.crop = readCrop(o.value(u"crop"));
            e.task.cropRadius = std::clamp(o.value(u"crop_radius").toDouble(), 0.0, 1.0);
            e.task.mirror = o.value(u"mirror").toBool();
            e.task.clipGroupId = o.value(u"clip_group_id").toString();
            e.task.clipId = o.value(u"clip_id").toString();
            e.proxy = o.value(u"proxy").toString();
            if (o.value(u"media").isObject())
                readMedia(o.value(u"media").toObject(), e);
            result.entries << e;
        }
    } else {
        // 转换输出的 stickers.json：只还原 文件、Emoji、关键词、圆角
        result.kind = Kind::StickersJson;
        for (const QJsonValue& v : stickers.toArray()) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.task.inputPath = o.value(u"file").toString();
            if (e.task.inputPath.isEmpty())
                continue;
            e.task.info.fileName = QFileInfo(e.task.inputPath).fileName();
            e.task.emoji = o.value(u"emoji").toString();
            e.task.keywords = readKeywords(o.value(u"keywords"));
            e.task.cropRadius = std::clamp(o.value(u"crop_radius").toDouble(), 0.0, 1.0);
            e.proxy = o.value(u"proxy").toString(); // 导出"已裁切源文件"时附带的预览副本
            result.entries << e;
        }
    }
    result.ok = true;
    result.error.clear();
    return result;
}

} // namespace tsm::StickerList
