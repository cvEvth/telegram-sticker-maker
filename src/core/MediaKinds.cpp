#include "core/MediaKinds.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace tsm::MediaKinds {

namespace {
QString extensionOf(const QString& path)
{
    return QFileInfo(path).suffix().toLower();
}

QString globList(const QStringList& extensions)
{
    QStringList globs;
    for (const QString& ext : extensions)
        globs << QStringLiteral("*.") + ext;
    return globs.join(u' ');
}
} // namespace

const QStringList& videoExtensions()
{
    static const QStringList list{u"mp4"_s, u"mkv"_s, u"webm"_s, u"mov"_s, u"avi"_s, u"flv"_s, u"m4v"_s};
    return list;
}

const QStringList& imageExtensions()
{
    static const QStringList list{u"png"_s, u"apng"_s, u"jpg"_s,  u"jpeg"_s, u"webp"_s, u"gif"_s,
                                  u"bmp"_s, u"tiff"_s, u"tif"_s, u"ico"_s,  u"heic"_s, u"heif"_s};
    return list;
}

MediaKind classify(const QString& path)
{
    const QString ext = extensionOf(path);
    if (videoExtensions().contains(ext))
        return MediaKind::Video;
    if (ext == u"gif" || ext == u"apng")
        return MediaKind::AnimatedImage;
    if (ext == u"png")
        return isAnimatedPng(path) ? MediaKind::AnimatedImage : MediaKind::StaticImage;
    if (ext == u"webp")
        return isAnimatedWebp(path) ? MediaKind::AnimatedImage : MediaKind::StaticImage;
    if (imageExtensions().contains(ext))
        return MediaKind::StaticImage;
    return MediaKind::Unsupported;
}

bool isSupportedMedia(const QString& path)
{
    const QString ext = extensionOf(path);
    return videoExtensions().contains(ext) || imageExtensions().contains(ext);
}

bool isImportFile(const QString& path)
{
    const QString ext = extensionOf(path);
    return ext == u"zip" || ext == u"json";
}

bool isAnimatedPng(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    static const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
    if (file.read(8) != signature)
        return false;
    // acTL 必须出现在第一个 IDAT 之前
    while (!file.atEnd()) {
        const QByteArray header = file.read(8);
        if (header.size() < 8)
            return false;
        const quint32 length = qFromBigEndian<quint32>(header.constData());
        const QByteArray type = header.mid(4, 4);
        if (type == "acTL")
            return true;
        if (type == "IDAT" || type == "IEND")
            return false;
        if (!file.seek(file.pos() + qint64(length) + 4)) // 数据 + CRC
            return false;
    }
    return false;
}

bool isAnimatedWebp(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(21);
    if (head.size() < 21 || !head.startsWith("RIFF") || head.mid(8, 4) != "WEBP")
        return false;
    // 扩展格式 VP8X 的标志字节里 bit1 表示动画
    return head.mid(12, 4) == "VP8X" && (static_cast<quint8>(head.at(20)) & 0x02);
}

QString openFileFilter()
{
    const QStringList all = videoExtensions() + imageExtensions();
    return QStringLiteral("媒体文件 (%1);;视频 (%2);;图片与动图 (%3);;所有文件 (*)")
        .arg(globList(all), globList(videoExtensions()), globList(imageExtensions()));
}

} // namespace tsm::MediaKinds
