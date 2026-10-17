#pragma once

#include "core/StickerTask.h"
#include "io/OutputPackager.h"
#include "media/StickerEncoder.h"

#include <QHash>
#include <QObject>
#include <QThreadPool>

namespace tsm {

struct ConvertSettings {
    OutputMode mode = OutputMode::Sticker;
    EncodePreset preset = EncodePreset::Anime;
    bool asZip = true;
    QString zipDir;   // 压缩包所在目录（输出目录，为空时用默认目录）
    QString workRoot; // 临时文件根目录
};

struct ConvertJob {
    StickerTask task;
    QString fileName;  // 目标文件名，如 "003_😂.webm"
    QString targetDir; // 目标目录
};

// 批量转换：最多 2 个任务并行，全部结束后统一打包输出。
// 所有信号都在主线程发出。
class BatchConverter : public QObject {
    Q_OBJECT

public:
    explicit BatchConverter(QObject* parent = nullptr);
    ~BatchConverter() override;

    bool isRunning() const { return m_running; }
    void start(const QList<ConvertJob>& jobs, const ConvertSettings& settings);
    void cancel();

signals:
    void taskStarted(const QString& taskId);
    void taskProgress(const QString& taskId, int percent, const QString& text);
    void taskFinished(const QString& taskId, tsm::TaskStatus status, const QString& text, qint64 size);
    void packaging();
    void finished(const tsm::OutputPackager::Result& result, int succeeded, int failed, int cancelled);

private:
    void onJobDone(const ConvertJob& job, const EncodeResult& result, const QString& stagedFile);
    void finishBatch();

    QThreadPool m_pool;
    CancelFlag m_cancel;
    ConvertSettings m_settings;
    QString m_batchDir;
    QList<OutputPackager::Item> m_succeededItems;
    QHash<QString, int> m_order; // taskId → 列表顺序，用于打包时保持顺序
    int m_remaining = 0;
    int m_succeeded = 0;
    int m_failed = 0;
    int m_cancelled = 0;
    bool m_running = false;
};

} // namespace tsm
