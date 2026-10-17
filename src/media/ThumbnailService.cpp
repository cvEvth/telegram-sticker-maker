#include "media/ThumbnailService.h"

#include "core/CropMath.h"
#include "media/FFmpegTools.h"
#include "media/ProcessRunner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

namespace tsm {

namespace {
constexpr int kMemoryCostLimit = 64 * 1024 * 1024; // 按字节计
}

ThumbnailService::ThumbnailService(QObject* parent)
    : QObject(parent)
    , m_memory(kMemoryCostLimit)
{
    m_pool.setMaxThreadCount(2);
}

ThumbnailService::~ThumbnailService()
{
    m_pool.clear();
    m_pool.waitForDone();
}

ThumbnailService::Request ThumbnailService::requestFor(const StickerTask& task, QSize box)
{
    Request r;
    r.path = task.inputPath;
    r.isVideo = task.info.isVideo;
    r.sourceAlpha = task.info.hasAlpha;
    r.videoCodec = task.info.videoCodec;
    r.time = task.startTime.value_or(0.0);
    if (task.crop)
        r.crop = CropMath::evenAligned(QRectF(*task.crop), QSize(task.info.width, task.info.height));
    r.radius = task.cropRadius;
    r.mirror = task.mirror;
    r.box = box;
    return r;
}

void ThumbnailService::setCacheDir(const QString& dir)
{
    m_cacheDir = dir;
    m_failed.clear();
}

QString ThumbnailService::keyFor(const Request& r)
{
    const QFileInfo fi(r.path);
    QString crop = QStringLiteral("-");
    if (r.crop)
        crop = QStringLiteral("%1,%2,%3,%4").arg(r.crop->x()).arg(r.crop->y()).arg(r.crop->width()).arg(r.crop->height());
    const QString raw = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8x%9")
                            .arg(fi.absoluteFilePath())
                            .arg(fi.lastModified().toMSecsSinceEpoch())
                            .arg(fi.size())
                            .arg(r.time, 0, 'f', 3)
                            .arg(crop)
                            .arg(r.radius, 0, 'f', 3)
                            .arg(r.mirror ? 1 : 0)
                            .arg(r.box.width())
                            .arg(r.box.height());
    return QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString ThumbnailService::diskPath(const QString& key) const
{
    return m_cacheDir.isEmpty() ? QString() : QDir(m_cacheDir).filePath(key + QStringLiteral(".png"));
}

QImage ThumbnailService::thumbnail(const Request& request)
{
    const QString key = keyFor(request);
    if (const QImage* cached = m_memory.object(key))
        return *cached;
    if (m_pending.contains(key) || m_failed.contains(key))
        return {};

    m_pending.insert(key);
    const QString disk = diskPath(key);
    m_pool.start([this, request, key, disk] {
        QImage image;
        if (!disk.isEmpty() && QFileInfo::exists(disk))
            image.load(disk);
        if (image.isNull()) {
            image = render(request);
            if (!image.isNull() && !disk.isEmpty()) {
                QDir().mkpath(QFileInfo(disk).absolutePath());
                image.save(disk, "PNG");
            }
        }
        QMetaObject::invokeMethod(this, [this, key, image] {
            m_pending.remove(key);
            if (image.isNull()) {
                m_failed.insert(key);
                return;
            }
            m_memory.insert(key, new QImage(image), int(std::min<qsizetype>(image.sizeInBytes(), kMemoryCostLimit)));
            emit ready(key);
        }, Qt::QueuedConnection);
    });
    return {};
}

QImage ThumbnailService::render(const Request& r, QString* error)
{
    if (!QFileInfo::exists(r.path)) {
        if (error)
            *error = QStringLiteral("文件不存在");
        return {};
    }
    QStringList filters;
    if (r.mirror)
        filters << QStringLiteral("hflip");
    if (r.crop)
        filters << QStringLiteral("crop=%1:%2:%3:%4").arg(r.crop->width()).arg(r.crop->height()).arg(r.crop->x()).arg(r.crop->y());
    filters << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease:flags=bicubic").arg(r.box.width()).arg(r.box.height())
            << QStringLiteral("format=rgba");

    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-loglevel"), QStringLiteral("error")};
    if (r.isVideo && r.time > 0)
        args << QStringLiteral("-ss") << QString::number(r.time, 'f', 3);
    if (r.sourceAlpha && r.videoCodec == u"vp9")
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9");
    args << QStringLiteral("-i") << QDir::toNativeSeparators(r.path) << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-vf") << filters.join(u',') << QStringLiteral("-f") << QStringLiteral("image2pipe")
         << QStringLiteral("-c:v") << QStringLiteral("png") << QStringLiteral("-");

    const ProcessResult result = ProcessRunner::run(FFmpegTools::ffmpegPath(), args, 30000);
    QImage frame;
    if (!result.ok() || !frame.loadFromData(result.stdOut, "PNG")) {
        if (error)
            *error = result.errorSummary();
        return {};
    }
    frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (r.radius <= 0)
        return frame;

    // 圆角外透明，与最终输出一致
    QImage shaped(frame.size(), QImage::Format_ARGB32_Premultiplied);
    shaped.fill(Qt::transparent);
    QPainter p(&shaped);
    p.setRenderHint(QPainter::Antialiasing);
    const double radius = std::min(frame.width(), frame.height()) / 2.0 * std::min(r.radius, 1.0);
    QPainterPath path;
    path.addRoundedRect(QRectF(frame.rect()), radius, radius);
    p.setClipPath(path);
    p.drawImage(0, 0, frame);
    p.end();
    return shaped;
}

void ThumbnailService::clearMemory()
{
    m_memory.clear();
    m_failed.clear();
}

} // namespace tsm
