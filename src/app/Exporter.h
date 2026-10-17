#pragma once

#include "core/StickerTask.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <atomic>
#include <memory>

namespace tsm {

// 导出（需求 9.1，不转码）
enum class ExportKind {
    Sources, // 模式 A：原片 + sticker_list.json（相对路径），附带预览缓存
    Trimmed, // 模式 B：按转换结果命名的已裁切源文件 + stickers.json
};

struct ExportSettings {
    ExportKind kind = ExportKind::Sources;
    bool asZip = true;
    QString destDir;       // 压缩包所在目录；模式 A 不压缩时为用户选的父文件夹
    QString workRoot;      // 模式 B 的临时文件根目录
    QString proxyCacheDir; // 预览缓存目录
};

struct ExportJob {
    StickerTask task;
    QString fileName;  // 模式 B：转换结果的文件名，如 "001_😂.webm"（扩展名按规则替换）
    QString targetDir; // 模式 B 文件夹输出时的目标目录
};

struct ExportSummary {
    QStringList locations; // 生成的压缩包 / 文件夹
    QStringList logLines;
    int exported = 0;
    int missing = 0;
    int failed = 0;
    int cancelled = 0;
};

// 逐个导出（单线程），所有信号在主线程发出。
class Exporter : public QObject {
    Q_OBJECT

public:
    static constexpr auto kBundlePrefix = "sticker_bundle";
    static constexpr auto kTrimmedPrefix = "TG_Trimmed";

    explicit Exporter(QObject* parent = nullptr);
    ~Exporter() override;

    bool isRunning() const { return m_running; }
    void start(const QList<ExportJob>& jobs, const ExportSettings& settings);
    void cancel();

signals:
    void taskStarted(const QString& taskId);
    void taskProgress(const QString& taskId, int percent, const QString& text);
    void taskFinished(const QString& taskId, tsm::TaskStatus status, const QString& text, qint64 size);
    void packaging(const QString& text);
    void finished(const tsm::ExportSummary& summary);

private:
    using Flag = std::shared_ptr<std::atomic_bool>;
    ExportSummary exportSources(const QList<ExportJob>& jobs, const ExportSettings& settings, const Flag& cancel);
    ExportSummary exportTrimmed(const QList<ExportJob>& jobs, const ExportSettings& settings, const Flag& cancel);

    void emitStarted(const QString& id);
    void emitProgress(const QString& id, int percent, const QString& text);
    void emitFinished(const QString& id, TaskStatus status, const QString& text, qint64 size = 0);
    void emitPackaging(const QString& text);

    QThreadPool m_pool;
    Flag m_cancel;
    bool m_running = false;
};

} // namespace tsm
