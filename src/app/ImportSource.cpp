#include "app/ImportSource.h"

#include "io/ZipReader.h"
#include "media/ProxyCache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tsm::ImportSource {

namespace {
const QString kDoneMarker = QStringLiteral(".extracted");

QString notFoundText()
{
    return QCoreApplication::translate("ImportSource", "未找到 sticker_list.json 或 stickers.json");
}
} // namespace

QString findManifestIn(const QString& dir)
{
    const QStringList names{QString::fromLatin1(kListFileName), QString::fromLatin1(kStickersFileName)};
    const QDir root(dir);
    for (const QString& name : names) {
        if (root.exists(name))
            return root.filePath(name);
    }
    for (const QString& sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir child(root.filePath(sub));
        for (const QString& name : names) {
            if (child.exists(name))
                return child.filePath(name);
        }
    }
    return {};
}

QString extractDirFor(const QString& zipPath, const QString& importCacheDir)
{
    const QFileInfo fi(zipPath);
    const QString identity = QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    return QDir(importCacheDir).filePath(fi.completeBaseName().left(40) + QStringLiteral("_") + hash);
}

Prepared prepare(const QString& path, const QString& importCacheDir, const std::shared_ptr<std::atomic_bool>& cancel)
{
    Prepared result;
    const QFileInfo fi(path);
    QString manifest;

    if (fi.isDir()) {
        manifest = findManifestIn(fi.absoluteFilePath());
    } else if (fi.suffix().compare(u"zip", Qt::CaseInsensitive) == 0) {
        const QString dir = extractDirFor(fi.absoluteFilePath(), importCacheDir);
        if (QFileInfo::exists(QDir(dir).filePath(kDoneMarker))) {
            result.reusedExtraction = true;
        } else {
            // 先解压到临时目录，完整成功后再改名，避免半截内容被当成可复用的缓存
            const QString partial = dir + QStringLiteral(".part");
            QDir(partial).removeRecursively();
            QDir(dir).removeRecursively();
            QString error;
            if (!ZipReader::extractAll(fi.absoluteFilePath(), partial, &error, cancel)) {
                QDir(partial).removeRecursively();
                result.error = QCoreApplication::translate("ImportSource", "解压失败: ") + error;
                return result;
            }
            QFile marker(QDir(partial).filePath(kDoneMarker));
            if (marker.open(QIODevice::WriteOnly))
                marker.close();
            if (!QDir().rename(partial, dir)) {
                QDir(partial).removeRecursively();
                result.error = QCoreApplication::translate("ImportSource", "无法写入导入缓存 %1").arg(QDir::toNativeSeparators(dir));
                return result;
            }
        }
        manifest = findManifestIn(dir);
    } else if (fi.isFile()) {
        manifest = fi.absoluteFilePath();
    } else {
        result.error = QCoreApplication::translate("ImportSource", "找不到 %1").arg(QDir::toNativeSeparators(path));
        return result;
    }

    if (manifest.isEmpty()) {
        result.error = notFoundText();
        return result;
    }
    QFile file(manifest);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QCoreApplication::translate("ImportSource", "无法读取 %1").arg(QDir::toNativeSeparators(manifest));
        return result;
    }
    result.list = StickerList::parse(file.readAll());
    if (!result.list.ok) {
        result.error = result.list.error;
        return result;
    }
    result.manifestPath = manifest;
    result.baseDir = QFileInfo(manifest).absolutePath();
    result.ok = true;
    return result;
}

bool installProxy(const QString& proxyFile, const QString& source, const QString& proxyCacheDir)
{
    if (proxyFile.isEmpty() || proxyCacheDir.isEmpty() || !QFileInfo(proxyFile).isFile())
        return false;
    const QString target = ProxyCache::pathIn(proxyCacheDir, source);
    if (QFileInfo::exists(target))
        return false;
    QDir().mkpath(proxyCacheDir);
    const QString temp = target + QStringLiteral(".part.mp4");
    QFile::remove(temp);
    if (!QFile::copy(proxyFile, temp))
        return false;
    if (!QFile::rename(temp, target)) {
        QFile::remove(temp);
        return false;
    }
    return true;
}

} // namespace tsm::ImportSource
