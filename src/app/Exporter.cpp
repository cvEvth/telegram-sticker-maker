#include "app/Exporter.h"

#include "app/ImportSource.h"
#include "core/OutputNaming.h"
#include "core/StickerList.h"
#include "io/OutputPackager.h"
#include "io/ZipWriter.h"
#include "media/ProxyCache.h"
#include "media/SourceTrimmer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace tsm {

namespace {
QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

bool copyReplacing(const QString& from, const QString& to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    return QFile::copy(from, to);
}
} // namespace

Exporter::Exporter(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

Exporter::~Exporter()
{
    cancel();
    m_pool.waitForDone();
}

void Exporter::start(const QList<ExportJob>& jobs, const ExportSettings& settings)
{
    if (m_running || jobs.isEmpty())
        return;
    m_running = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const Flag cancel = m_cancel;
    m_pool.start([this, jobs, settings, cancel] {
        const ExportSummary summary = settings.kind == ExportKind::Sources ? exportSources(jobs, settings, cancel)
                                                                             : exportTrimmed(jobs, settings, cancel);
        QMetaObject::invokeMethod(this, [this, summary] {
            m_running = false;
            emit finished(summary);
        }, Qt::QueuedConnection);
    });
}

void Exporter::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

void Exporter::emitStarted(const QString& id)
{
    QMetaObject::invokeMethod(this, [this, id] { emit taskStarted(id); }, Qt::QueuedConnection);
}

void Exporter::emitProgress(const QString& id, int percent, const QString& text)
{
    QMetaObject::invokeMethod(this, [this, id, percent, text] { emit taskProgress(id, percent, text); }, Qt::QueuedConnection);
}

void Exporter::emitFinished(const QString& id, TaskStatus status, const QString& text, qint64 size)
{
    QMetaObject::invokeMethod(this, [this, id, status, text, size] { emit taskFinished(id, status, text, size); },
                              Qt::QueuedConnection);
}

void Exporter::emitPackaging(const QString& text)
{
    QMetaObject::invokeMethod(this, [this, text] { emit packaging(text); }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- 模式 A：原片 + 参数清单

ExportSummary Exporter::exportSources(const QList<ExportJob>& jobs, const ExportSettings& settings, const Flag& cancel)
{
    ExportSummary summary;
    const QDateTime now = QDateTime::currentDateTime();
    const QString baseName = OutputNaming::timestampedName(QString::fromLatin1(kBundlePrefix), now);
    QDir().mkpath(settings.destDir);
    QString zipPath;
    QString folder;
    if (settings.asZip) {
        zipPath = QDir(settings.destDir).filePath(OutputNaming::uniqueFileName(settings.destDir, baseName + QStringLiteral(".zip")));
    } else {
        folder = QDir(settings.destDir).filePath(OutputNaming::uniqueFileName(settings.destDir, baseName));
        if (!QDir().mkpath(folder)) {
            summary.logLines << QStringLiteral("❌ 无法创建文件夹 %1").arg(native(folder));
            for (const ExportJob& job : jobs)
                emitFinished(job.task.id, TaskStatus::Failed, QStringLiteral("无法创建导出文件夹"));
            summary.failed = int(jobs.size());
            return summary;
        }
    }

    // 同一个源文件只复制一次，重名加后缀
    QHash<QString, QString> sourceRel; // 绝对路径 → sources/xxx
    QHash<QString, QString> proxyRel;  // 绝对路径 → proxies/xxx.mp4
    QSet<QString> taken;
    QList<StickerTask> listed;
    QList<ZipWriter::Entry> entries;

    for (const ExportJob& job : jobs) {
        const QString id = job.task.id;
        if (cancel->load()) {
            ++summary.cancelled;
            emitFinished(id, TaskStatus::Cancelled, QStringLiteral("已取消"));
            continue;
        }
        emitStarted(id);
        const QFileInfo fi(job.task.inputPath);
        const QString abs = fi.absoluteFilePath();
        if (!fi.isFile()) {
            ++summary.missing;
            emitFinished(id, TaskStatus::Failed, QStringLiteral("源文件缺失"));
            continue;
        }
        if (!sourceRel.contains(abs)) {
            const QString name = OutputNaming::uniqueName(fi.fileName(), taken);
            taken.insert(name.toLower());
            const QString rel = QStringLiteral("sources/") + name;
            const QString proxy = settings.proxyCacheDir.isEmpty() ? QString() : ProxyCache::pathIn(settings.proxyCacheDir, abs);
            const bool hasProxy = !proxy.isEmpty() && QFileInfo(proxy).isFile();
            const QString proxyName = QStringLiteral("proxies/") + name + QStringLiteral(".mp4");
            if (settings.asZip) {
                entries << ZipWriter::Entry{rel, abs, {}};
                if (hasProxy)
                    entries << ZipWriter::Entry{proxyName, proxy, {}};
            } else {
                emitProgress(id, 10, QStringLiteral("复制原片..."));
                if (!copyReplacing(abs, QDir(folder).filePath(rel))) {
                    ++summary.failed;
                    emitFinished(id, TaskStatus::Failed, QStringLiteral("复制失败"));
                    continue;
                }
                if (hasProxy && !copyReplacing(proxy, QDir(folder).filePath(proxyName)))
                    summary.logLines << QStringLiteral("⚠️ 无法复制预览缓存 %1").arg(native(proxy));
            }
            sourceRel.insert(abs, rel);
            if (hasProxy)
                proxyRel.insert(abs, proxyName);
        }
        listed << job.task;
        ++summary.exported;
        emitFinished(id, TaskStatus::Succeeded, QStringLiteral("已导出"), fi.size());
    }

    if (listed.isEmpty()) {
        if (!folder.isEmpty())
            QDir(folder).removeRecursively();
        return summary;
    }

    StickerList::WriteOptions options;
    options.exportMode = QStringLiteral("sources");
    options.exportedAt = now;
    options.pathFor = [&](const StickerTask& t) { return sourceRel.value(QFileInfo(t.inputPath).absoluteFilePath()); };
    options.proxyFor = [&](const StickerTask& t) { return proxyRel.value(QFileInfo(t.inputPath).absoluteFilePath()); };
    const QByteArray json = StickerList::write(listed, options);
    const QString listName = QString::fromLatin1(ImportSource::kListFileName);

    if (settings.asZip) {
        entries << ZipWriter::Entry{listName, {}, json};
        emitPackaging(QStringLiteral("正在写入压缩包..."));
        int lastPercent = -1;
        QString error;
        const bool ok = ZipWriter::writeStored(zipPath, entries, &error, [&](double f) {
            const int percent = int(f * 100);
            if (percent != lastPercent) {
                lastPercent = percent;
                emitPackaging(QStringLiteral("正在写入压缩包 %1%").arg(percent));
            }
            return !cancel->load();
        });
        if (!ok) {
            summary.logLines << QStringLiteral("❌ 打包失败: %1").arg(error);
            const bool stopped = cancel->load();
            for (const StickerTask& t : listed)
                emitFinished(t.id, stopped ? TaskStatus::Cancelled : TaskStatus::Failed,
                             stopped ? QStringLiteral("已取消") : QStringLiteral("打包失败"));
            (stopped ? summary.cancelled : summary.failed) += summary.exported;
            summary.exported = 0;
            return summary;
        }
        summary.locations << zipPath;
        summary.logLines << QStringLiteral("📦 已打包 %1 项（%2 个原片）: %3").arg(listed.size()).arg(sourceRel.size()).arg(native(zipPath));
    } else {
        QSaveFile file(QDir(folder).filePath(listName));
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
            summary.logLines << QStringLiteral("❌ 无法写入 %1").arg(native(QDir(folder).filePath(listName)));
        summary.locations << folder;
        summary.logLines << QStringLiteral("📁 已导出 %1 项（%2 个原片）到: %3").arg(listed.size()).arg(sourceRel.size()).arg(native(folder));
    }
    return summary;
}

// ---------------------------------------------------------------- 模式 B：已裁切的转换前文件

ExportSummary Exporter::exportTrimmed(const QList<ExportJob>& jobs, const ExportSettings& settings, const Flag& cancel)
{
    ExportSummary summary;
    const QString batchDir = QDir(settings.workRoot)
                                 .filePath(QStringLiteral("export_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    QList<OutputPackager::Item> items;

    for (int i = 0; i < jobs.size(); ++i) {
        const ExportJob& job = jobs.at(i);
        const QString id = job.task.id;
        if (cancel->load()) {
            ++summary.cancelled;
            emitFinished(id, TaskStatus::Cancelled, QStringLiteral("已取消"));
            continue;
        }
        emitStarted(id);
        if (!QFileInfo(job.task.inputPath).isFile()) {
            ++summary.missing;
            emitFinished(id, TaskStatus::Failed, QStringLiteral("源文件缺失"));
            continue;
        }
        const QString workDir = QDir(batchDir).filePath(QStringLiteral("task_%1").arg(i + 1));
        const QString outputBase = QDir(batchDir).filePath(QStringLiteral("out_%1").arg(i + 1));
        const SourceTrimmer::Result r = SourceTrimmer::run(job.task, workDir, outputBase, cancel,
                                                           [&](int percent, const QString& text) { emitProgress(id, percent, text); });
        QDir(workDir).removeRecursively();
        if (r.cancelled) {
            ++summary.cancelled;
            emitFinished(id, TaskStatus::Cancelled, QStringLiteral("已取消"));
            continue;
        }
        if (!r.ok) {
            ++summary.failed;
            emitFinished(id, TaskStatus::Failed, r.error);
            summary.logLines << QStringLiteral("❌ [%1] %2").arg(job.task.info.fileName, r.error);
            continue;
        }

        OutputPackager::Item item;
        item.taskId = id;
        item.stagedFile = r.file;
        item.keepSource = r.isSourceCopy;
        item.fileName = QFileInfo(job.fileName).completeBaseName() + u'.' + r.extension;
        item.targetDir = job.targetDir;
        item.emoji = job.task.emoji;
        item.keywords = job.task.keywords;
        item.cropRadius = job.task.cropRadius;
        // 整段复制的视频附带它的预览缓存
        if (r.isSourceCopy && job.task.info.isVideo && !settings.proxyCacheDir.isEmpty()) {
            const QString proxy = ProxyCache::pathIn(settings.proxyCacheDir, QFileInfo(job.task.inputPath).absoluteFilePath());
            if (QFileInfo(proxy).isFile())
                item.proxyFile = proxy;
        }
        items << item;
        ++summary.exported;
        emitFinished(id, TaskStatus::Succeeded, r.note, QFileInfo(r.file).size());
    }

    if (!items.isEmpty()) {
        emitPackaging(QStringLiteral("正在写出文件..."));
        OutputPackager::Options options;
        options.prefix = QString::fromLatin1(kTrimmedPrefix);
        options.noun = QStringLiteral("文件");
        const OutputPackager::Result result =
            OutputPackager::package(items, settings.asZip, settings.destDir, QDateTime::currentDateTime(), options);
        summary.locations = result.locations;
        summary.logLines << result.logLines;
        if (!result.ok)
            summary.logLines << QStringLiteral("❌ 导出失败: %1").arg(result.error);
    }
    QDir(batchDir).removeRecursively();
    return summary;
}

} // namespace tsm
