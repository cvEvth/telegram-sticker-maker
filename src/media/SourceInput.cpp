#include "media/SourceInput.h"

#include "core/ImageHeaders.h"
#include "core/MediaKinds.h"
#include "media/FfmpegRun.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>

#include <webp/demux.h>

namespace tsm::SourceInput {

namespace {
QMutex g_mutex;   // 保护 g_dir 与 g_locks
QString g_dir;
QHash<QString, std::shared_ptr<QMutex>> g_locks; // 同一个源文件只解码一次

std::shared_ptr<QMutex> lockFor(const QString& key)
{
    QMutexLocker locker(&g_mutex);
    auto& lock = g_locks[key];
    if (!lock)
        lock = std::make_shared<QMutex>();
    return lock;
}
} // namespace

void setCacheDir(const QString& dir)
{
    QMutexLocker locker(&g_mutex);
    g_dir = dir;
}

QString cacheDir()
{
    QMutexLocker locker(&g_mutex);
    if (!g_dir.isEmpty())
        return g_dir;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("tsm_decoded"));
}

bool decodeAnimatedWebp(const QString& source, const QString& output, QString* error)
{
    const auto fail = [error](const QString& text) {
        if (error)
            *error = text;
        return false;
    };
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QCoreApplication::translate("SourceInput", "无法读取 %1").arg(QDir::toNativeSeparators(source)));
    const QByteArray bytes = file.readAll();

    WebPData data{reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size())};
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options))
        return fail(QCoreApplication::translate("SourceInput", "libwebp 版本不匹配"));
    options.color_mode = MODE_RGBA;
    options.use_threads = 1;
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&data, &options),
                                                                               &WebPAnimDecoderDelete);
    if (!decoder)
        return fail(QCoreApplication::translate("SourceInput", "无法解码动态 WebP"));
    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(decoder.get(), &info) || info.frame_count == 0)
        return fail(QCoreApplication::translate("SourceInput", "无法解码动态 WebP"));

    QTemporaryDir frames(QDir(QFileInfo(output).absolutePath()).filePath(QStringLiteral("frames_XXXXXX")));
    if (!frames.isValid())
        return fail(QCoreApplication::translate("SourceInput", "无法创建临时目录"));

    // ffconcat：每帧一张 PNG + 时长；最后一帧再写一次让时长生效
    QByteArray list = "ffconcat version 1.0\n";
    int prev = 0;
    int index = 0;
    QString lastName;
    while (WebPAnimDecoderHasMoreFrames(decoder.get())) {
        uint8_t* rgba = nullptr;
        int timestamp = 0; // 本帧结束时刻（毫秒）
        if (!WebPAnimDecoderGetNext(decoder.get(), &rgba, &timestamp))
            return fail(QCoreApplication::translate("SourceInput", "动态 WebP 第 %1 帧解码失败").arg(index + 1));
        int durationMs = timestamp - prev;
        prev = timestamp;
        if (durationMs <= 0)
            durationMs = ImageHeaders::kDefaultFrameMs;
        const QImage image(rgba, int(info.canvas_width), int(info.canvas_height), int(info.canvas_width) * 4,
                           QImage::Format_RGBA8888);
        lastName = QStringLiteral("f%1.png").arg(index++, 5, 10, QLatin1Char('0'));
        if (!image.save(frames.filePath(lastName), "PNG"))
            return fail(QCoreApplication::translate("SourceInput", "无法写入临时帧"));
        list += "file '" + lastName.toUtf8() + "'\nduration " + QByteArray::number(durationMs / 1000.0, 'f', 3) + "\n";
    }
    list += "file '" + lastName.toUtf8() + "'\n";
    const QString listPath = frames.filePath(QStringLiteral("list.ffconcat"));
    QFile listFile(listPath);
    if (!listFile.open(QIODevice::WriteOnly) || listFile.write(list) != list.size())
        return fail(QCoreApplication::translate("SourceInput", "无法写入帧列表"));
    listFile.close();

    const QString temp = output + QStringLiteral(".part.mkv");
    const FfmpegRun::Result run = FfmpegRun::run(
        {QStringLiteral("-f"), QStringLiteral("concat"), QStringLiteral("-safe"), QStringLiteral("0"), QStringLiteral("-i"),
         QDir::toNativeSeparators(listPath), QStringLiteral("-fps_mode"), QStringLiteral("passthrough"), QStringLiteral("-c:v"),
         QStringLiteral("png"), QStringLiteral("-pix_fmt"), QStringLiteral("rgba"), QStringLiteral("-f"),
         QStringLiteral("matroska"), QDir::toNativeSeparators(temp)},
        0, {});
    if (!run.ok) {
        QFile::remove(temp);
        return fail(QCoreApplication::translate("SourceInput", "生成中间文件失败: ") + run.error);
    }
    QFile::remove(output);
    if (!QFile::rename(temp, output)) {
        QFile::remove(temp);
        return fail(QCoreApplication::translate("SourceInput", "无法写入中间文件"));
    }
    return true;
}

QString resolve(const QString& path)
{
    if (MediaKinds::classify(path) != MediaKind::AnimatedImage || QFileInfo(path).suffix().compare(u"webp", Qt::CaseInsensitive) != 0)
        return path;
    const QFileInfo fi(path);
    const QString identity = QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const QString dir = cacheDir();
    const QString output = QDir(dir).filePath(fi.completeBaseName().left(40) + u'_' + hash + QStringLiteral(".mkv"));

    const auto lock = lockFor(output);
    QMutexLocker locker(lock.get());
    if (QFileInfo(output).isFile())
        return output;
    QDir().mkpath(dir);
    QString error;
    if (!decodeAnimatedWebp(path, output, &error)) {
        qWarning("animated webp decode failed: %s", qPrintable(error));
        return path;
    }
    return output;
}

} // namespace tsm::SourceInput
