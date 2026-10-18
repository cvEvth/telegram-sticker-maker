#include "io/OutputPackager.h"

#include "core/OutputNaming.h"
#include "core/StickerTask.h"
#include "io/ZipWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>

namespace tsm::OutputPackager {

namespace {
const QString kJsonName = QStringLiteral("stickers.json");

// 同一批里的文件名去重（_2、_3……）
QStringList uniqueNames(const QList<Item>& items)
{
    QSet<QString> taken{kJsonName.toLower()};
    QStringList names;
    for (const Item& item : items) {
        const QString name = OutputNaming::uniqueName(item.fileName, taken);
        taken.insert(name.toLower());
        names << name;
    }
    return names;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

bool moveFile(const QString& from, const QString& to)
{
    return QFile::rename(from, to) || (QFile::copy(from, to) && (QFile::remove(from), true));
}

bool transfer(const Item& item, const QString& to)
{
    return item.keepSource ? QFile::copy(item.stagedFile, to) : moveFile(item.stagedFile, to);
}

// 预览副本复制到 folder/proxies/
void copyProxy(const Item& item, const QString& folder, const QString& fileName, Result& result)
{
    if (item.proxyFile.isEmpty())
        return;
    const QString target = QDir(folder).filePath(proxyEntryName(fileName));
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QFile::copy(item.proxyFile, target))
        result.logLines << QCoreApplication::translate("OutputPackager", "⚠️ 无法复制预览缓存 %1").arg(native(target));
}

// 退回方案：单个文件直接写到各自的目标目录
void writeLoose(const QList<Item>& items, Result& result)
{
    for (const Item& item : items) {
        QDir().mkpath(item.targetDir);
        const QString path = QDir(item.targetDir).filePath(OutputNaming::uniqueFileName(item.targetDir, item.fileName));
        if (transfer(item, path))
            result.finalPaths.insert(item.taskId, path);
        else
            result.logLines << QCoreApplication::translate("OutputPackager", "❌ 无法写入 %1").arg(native(path));
    }
    QStringList dirs;
    for (const Item& item : items) {
        if (!dirs.contains(item.targetDir))
            dirs << item.targetDir;
    }
    result.locations = dirs;
    result.ok = !result.finalPaths.isEmpty();
}

void packageZip(const QList<Item>& items, const QString& zipDir, const QDateTime& now, const Options& options, Result& result)
{
    QDir().mkpath(zipDir);
    const QString zipName = OutputNaming::uniqueFileName(zipDir, OutputNaming::timestampedName(options.prefix, now) + QStringLiteral(".zip"));
    const QString zipPath = QDir(zipDir).filePath(zipName);
    const QStringList names = uniqueNames(items);

    QList<ZipWriter::Entry> entries;
    for (int i = 0; i < items.size(); ++i) {
        entries << ZipWriter::Entry{names.at(i), items.at(i).stagedFile, {}};
        if (!items.at(i).proxyFile.isEmpty())
            entries << ZipWriter::Entry{proxyEntryName(names.at(i)), items.at(i).proxyFile, {}};
    }
    entries << ZipWriter::Entry{kJsonName, {}, stickersJson(items, names)};

    QString error;
    if (ZipWriter::writeStored(zipPath, entries, &error)) {
        result.ok = true;
        result.locations << zipPath;
        for (const Item& item : items)
            result.finalPaths.insert(item.taskId, zipPath);
        result.logLines << (options.stickers ? QCoreApplication::translate("OutputPackager", "📦 已打包 %n 个贴纸: %1", nullptr, int(items.size()))
                                             : QCoreApplication::translate("OutputPackager", "📦 已打包 %n 个文件: %1", nullptr, int(items.size())))
                                .arg(native(zipPath));
        return;
    }
    result.logLines << QCoreApplication::translate("OutputPackager", "⚠️ 打包失败（%1），改为直接写出单个文件").arg(error);
    writeLoose(items, result);
}

void packageFolders(const QList<Item>& items, const QDateTime& now, const Options& options, Result& result)
{
    // 按目标目录分组，保持列表顺序
    QMap<QString, QList<Item>> groups;
    QStringList order;
    for (const Item& item : items) {
        const QString key = QDir::cleanPath(item.targetDir);
        if (!groups.contains(key))
            order << key;
        groups[key] << item;
    }

    const QString folderName = OutputNaming::timestampedName(options.prefix, now);
    for (const QString& dir : order) {
        const QList<Item>& group = groups.value(dir);
        QDir().mkpath(dir);
        const QString folder = QDir(dir).filePath(OutputNaming::uniqueFileName(dir, folderName));
        if (!QDir().mkpath(folder)) {
            result.logLines << QCoreApplication::translate("OutputPackager", "❌ 无法创建文件夹 %1").arg(native(folder));
            continue;
        }
        const QStringList names = uniqueNames(group);
        QList<Item> written;
        QStringList writtenNames;
        for (int i = 0; i < group.size(); ++i) {
            const QString path = QDir(folder).filePath(names.at(i));
            if (transfer(group.at(i), path)) {
                copyProxy(group.at(i), folder, names.at(i), result);
                result.finalPaths.insert(group.at(i).taskId, path);
                written << group.at(i);
                writtenNames << names.at(i);
            } else {
                result.logLines << QCoreApplication::translate("OutputPackager", "❌ 无法写入 %1").arg(native(path));
            }
        }
        if (written.isEmpty()) {
            QDir(folder).removeRecursively(); // 一个成功的都没有就删掉空文件夹
            continue;
        }
        if (!writeFile(QDir(folder).filePath(kJsonName), stickersJson(written, writtenNames)))
            result.logLines << QCoreApplication::translate("OutputPackager", "⚠️ 无法写入 %1").arg(native(QDir(folder).filePath(kJsonName)));
        result.locations << folder;
        result.logLines << (options.stickers ? QCoreApplication::translate("OutputPackager", "📁 已输出 %n 个贴纸到: %1", nullptr, int(written.size()))
                                             : QCoreApplication::translate("OutputPackager", "📁 已输出 %n 个文件到: %1", nullptr, int(written.size())))
                                .arg(native(folder));
    }
    result.ok = !result.finalPaths.isEmpty();
}
} // namespace

QByteArray stickersJson(const QList<Item>& items, const QStringList& fileNames)
{
    QJsonArray stickers;
    for (int i = 0; i < items.size(); ++i) {
        const Item& item = items.at(i);
        QJsonObject obj;
        obj.insert(QStringLiteral("file"), fileNames.value(i, item.fileName));
        obj.insert(QStringLiteral("emoji"), item.emoji);
        obj.insert(QStringLiteral("keywords"), QJsonArray::fromStringList(item.keywords));
        obj.insert(QStringLiteral("crop_radius"), item.cropRadius);
        obj.insert(QStringLiteral("shape"), shapeLabel(item.cropRadius));
        if (!item.proxyFile.isEmpty())
            obj.insert(QStringLiteral("proxy"), proxyEntryName(fileNames.value(i, item.fileName)));
        stickers.append(obj);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("stickers"), stickers}}).toJson(QJsonDocument::Indented);
}

QString proxyEntryName(const QString& fileName)
{
    return QStringLiteral("proxies/") + fileName + QStringLiteral(".mp4");
}

Result package(const QList<Item>& items, bool asZip, const QString& zipDir, const QDateTime& now, const Options& options)
{
    Result result;
    if (items.isEmpty()) {
        result.error = options.stickers ? QCoreApplication::translate("OutputPackager", "没有可输出的贴纸")
                                        : QCoreApplication::translate("OutputPackager", "没有可输出的文件");
        return result;
    }
    if (asZip)
        packageZip(items, zipDir, now, options, result);
    else
        packageFolders(items, now, options, result);
    if (!result.ok && result.error.isEmpty())
        result.error = QCoreApplication::translate("OutputPackager", "输出失败");
    return result;
}

} // namespace tsm::OutputPackager
