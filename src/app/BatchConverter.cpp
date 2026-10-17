#include "app/BatchConverter.h"

#include "core/Spec.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace tsm {

BatchConverter::BatchConverter(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(spec::kMaxParallelJobs);
}

BatchConverter::~BatchConverter()
{
    cancel();
    m_pool.waitForDone();
}

void BatchConverter::start(const QList<ConvertJob>& jobs, const ConvertSettings& settings)
{
    if (m_running || jobs.isEmpty())
        return;
    m_running = true;
    m_settings = settings;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_succeededItems.clear();
    m_order.clear();
    m_remaining = int(jobs.size());
    m_succeeded = m_failed = m_cancelled = 0;
    m_batchDir = QDir(settings.workRoot).filePath(QStringLiteral("batch_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    QDir().mkpath(m_batchDir);

    for (int i = 0; i < jobs.size(); ++i) {
        const ConvertJob job = jobs.at(i);
        m_order.insert(job.task.id, i);
        const CancelFlag cancel = m_cancel;
        const QString taskDir = QDir(m_batchDir).filePath(QStringLiteral("task_%1").arg(i + 1));
        const QString staged = QDir(m_batchDir).filePath(QStringLiteral("out_%1.%2").arg(i + 1).arg(job.task.outputExtension()));
        const ConvertSettings snapshot = settings;

        m_pool.start([this, job, cancel, taskDir, staged, snapshot] {
            if (cancel->load()) {
                EncodeResult skipped;
                skipped.cancelled = true;
                skipped.statusText = QStringLiteral("已取消");
                QMetaObject::invokeMethod(this, [this, job, skipped] { onJobDone(job, skipped, {}); }, Qt::QueuedConnection);
                return;
            }
            const QString id = job.task.id;
            QMetaObject::invokeMethod(this, [this, id] { emit taskStarted(id); }, Qt::QueuedConnection);

            EncodeRequest request;
            request.task = job.task;
            request.mode = snapshot.mode;
            request.preset = snapshot.preset;
            request.workDir = taskDir;
            request.outputFile = staged;
            StickerEncoder encoder(request, cancel, [this, id](int percent, const QString& text) {
                QMetaObject::invokeMethod(this, [this, id, percent, text] { emit taskProgress(id, percent, text); }, Qt::QueuedConnection);
            });
            const EncodeResult result = encoder.run();
            QDir(taskDir).removeRecursively();
            QMetaObject::invokeMethod(this, [this, job, result, staged] { onJobDone(job, result, staged); }, Qt::QueuedConnection);
        });
    }
}

void BatchConverter::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

void BatchConverter::onJobDone(const ConvertJob& job, const EncodeResult& result, const QString& stagedFile)
{
    TaskStatus status = TaskStatus::Failed;
    if (result.ok) {
        status = TaskStatus::Succeeded;
        ++m_succeeded;
        OutputPackager::Item item;
        item.taskId = job.task.id;
        item.stagedFile = stagedFile;
        item.fileName = job.fileName;
        item.targetDir = job.targetDir;
        item.emoji = job.task.emoji;
        item.keywords = job.task.keywords;
        item.cropRadius = job.task.cropRadius;
        m_succeededItems << item;
    } else if (result.cancelled) {
        status = TaskStatus::Cancelled;
        ++m_cancelled;
    } else {
        ++m_failed;
    }
    emit taskFinished(job.task.id, status, result.statusText, result.size);

    if (--m_remaining == 0)
        finishBatch();
}

void BatchConverter::finishBatch()
{
    std::sort(m_succeededItems.begin(), m_succeededItems.end(),
              [this](const auto& a, const auto& b) { return m_order.value(a.taskId) < m_order.value(b.taskId); });
    if (m_succeededItems.isEmpty()) {
        QDir(m_batchDir).removeRecursively();
        m_running = false;
        emit finished(OutputPackager::Result{}, m_succeeded, m_failed, m_cancelled);
        return;
    }

    emit packaging();
    const QList<OutputPackager::Item> items = m_succeededItems;
    const bool asZip = m_settings.asZip;
    const QString zipDir = m_settings.zipDir;
    const QString batchDir = m_batchDir;
    m_pool.start([this, items, asZip, zipDir, batchDir] {
        const OutputPackager::Result result = OutputPackager::package(items, asZip, zipDir, QDateTime::currentDateTime());
        QDir(batchDir).removeRecursively();
        QMetaObject::invokeMethod(this, [this, result] {
            m_running = false;
            emit finished(result, m_succeeded, m_failed, m_cancelled);
        }, Qt::QueuedConnection);
    });
}

} // namespace tsm
