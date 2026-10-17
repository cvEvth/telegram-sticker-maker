#include "media/ProxyCache.h"

#include "core/Format.h"
#include "core/MediaKinds.h"
#include "media/FFmpegTools.h"
#include "media/ProcessRunner.h"
#include "media/SourceInput.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>

namespace tsm {

namespace {
constexpr int kProxyHeight = 480;
}

ProxyCache::ProxyCache(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

ProxyCache::~ProxyCache()
{
    cancelAll();
    m_pool.waitForDone();
}

void ProxyCache::setCacheDir(const QString& dir)
{
    m_dir = dir;
}

bool ProxyCache::needsProxy(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QStringList native{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("ogg"), QStringLiteral("ogv")};
    return !native.contains(ext);
}

bool ProxyCache::playerCannotDecode(const QString& path)
{
    return MediaKinds::classify(path) == MediaKind::AnimatedImage
        && QFileInfo(path).suffix().compare(u"webp", Qt::CaseInsensitive) == 0;
}

QString ProxyCache::pathIn(const QString& dir, const QString& source)
{
    const QFileInfo fi(source);
    const QString identity = QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    return QDir(dir).filePath(fi.completeBaseName().left(40) + QStringLiteral("_") + hash + QStringLiteral(".mp4"));
}

QString ProxyCache::cached(const QString& source) const
{
    if (m_dir.isEmpty())
        return {};
    const QString path = proxyPathFor(source);
    if (!QFileInfo::exists(path))
        return {};
    QFile file(path);
    if (file.open(QIODevice::ReadWrite))
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return path;
}

void ProxyCache::generate(const QString& source, double duration)
{
    if (m_dir.isEmpty() || m_jobs.contains(source))
        return;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_jobs.insert(source, cancel);
    const QString target = proxyPathFor(source);
    QDir().mkpath(m_dir);

    m_pool.start([this, source, target, duration, cancel] {
        const QString temp = target + QStringLiteral(".part.mp4");
        QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                         QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
                         QStringLiteral("-nostats"), QStringLiteral("-i"), QDir::toNativeSeparators(SourceInput::resolve(source)),
                         QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a:0?"),
                         QStringLiteral("-vf"), QStringLiteral("scale=-2:'min(%1,ih)':flags=bicubic,format=yuv420p").arg(kProxyHeight),
                         // 关键帧间隔小，拖动流畅
                         QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("4"),
                         QStringLiteral("-g"), QStringLiteral("12"), QStringLiteral("-bf"), QStringLiteral("0"),
                         QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k"),
                         QStringLiteral("-movflags"), QStringLiteral("+faststart"), QDir::toNativeSeparators(temp)};
        QProcess process;
        ProcessRunner::hideConsoleWindow(process);
        process.start(FFmpegTools::ffmpegPath(), args);
        QString error;
        bool ok = process.waitForStarted(10000);
        if (!ok)
            error = process.errorString();
        QByteArray pending;
        int last = -1;
        while (ok && process.state() != QProcess::NotRunning) {
            if (cancel->load()) {
                process.kill();
                process.waitForFinished(3000);
                ok = false;
                error = QStringLiteral("已取消");
                break;
            }
            process.waitForReadyRead(200);
            pending += process.readAllStandardOutput();
            qsizetype nl;
            while ((nl = pending.indexOf('\n')) >= 0) {
                const QByteArray line = pending.left(nl).trimmed();
                pending.remove(0, nl + 1);
                if (duration > 0 && line.startsWith("out_time_us=")) {
                    const int percent = std::clamp(int(line.mid(12).toDouble() / 1e6 / duration * 100), 0, 99);
                    if (percent != last) {
                        last = percent;
                        QMetaObject::invokeMethod(this, [this, source, percent] { emit progress(source, percent); }, Qt::QueuedConnection);
                    }
                }
            }
        }
        if (ok && (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)) {
            ok = false;
            const QStringList lines = QString::fromUtf8(process.readAllStandardError()).split(u'\n', Qt::SkipEmptyParts);
            error = lines.isEmpty() ? QStringLiteral("ffmpeg 退出码 %1").arg(process.exitCode()) : lines.last().trimmed();
        }
        if (ok) {
            QFile::remove(target);
            ok = QFile::rename(temp, target);
            if (!ok)
                error = QStringLiteral("无法写入缓存文件");
        }
        if (!ok)
            QFile::remove(temp);
        QMetaObject::invokeMethod(this, [this, source, ok, target, error] {
            m_jobs.remove(source);
            if (ok)
                prune();
            emit finished(source, ok, ok ? target : QString(), error);
        }, Qt::QueuedConnection);
    });
}

void ProxyCache::cancelAll()
{
    for (const auto& flag : std::as_const(m_jobs))
        flag->store(true);
}

void ProxyCache::prune()
{
    QFileInfoList files = QDir(m_dir).entryInfoList({QStringLiteral("*.mp4")}, QDir::Files);
    files.erase(std::remove_if(files.begin(), files.end(), [](const QFileInfo& f) { return f.fileName().endsWith(QStringLiteral(".part.mp4")); }),
                files.end());
    if (files.size() <= kMaxEntries)
        return;
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
    for (qsizetype i = kMaxEntries; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

QString CacheUsage::text() const
{
    return files == 0 ? QStringLiteral("缓存为空") : QStringLiteral("%1 个文件 · %2").arg(files).arg(formatBytes(bytes));
}

CacheUsage measurePreviewCache(const QString& dir)
{
    CacheUsage usage;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++usage.files;
        usage.bytes += it.fileInfo().size();
    }
    return usage;
}

bool clearPreviewCache(const QString& dir)
{
    QDir d(dir);
    return !d.exists() || d.removeRecursively();
}

} // namespace tsm
