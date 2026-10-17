#pragma once

#include "app/AppSettings.h"
#include "app/ImportSource.h"
#include "app/ListLoader.h"
#include "media/MediaProbe.h"

#include <QMainWindow>
#include <QThreadPool>
#include <atomic>
#include <memory>

class QPushButton;
class QStackedWidget;

namespace tsm {

class BatchConverter;
class DropOverlay;
class DropZone;
class Exporter;
struct ExportSummary;
class HeaderBar;
class LogPanel;
class ProxyCache;
class SessionStore;
class SettingsPanel;
class TaskListModel;
class TaskTableView;
class ThumbnailService;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 启动参数里的文件 / 文件夹（拖到程序图标上打开）
    void openPaths(const QStringList& paths) { addPaths(paths); }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Busy { None, Converting, Exporting, Importing, Restoring };

    void buildUi();
    QWidget* buildToolbar();
    void configureTable();
    void connectSignals();

    // 添加素材
    void addFilesDialog();
    void addFolderDialog();
    void importDialog();
    void addPaths(const QStringList& paths);
    void importManifest(const QString& path);
    void onImportPrepared(const QString& path, const ImportSource::Prepared& prepared, bool cancelled);
    void startProbing(const QStringList& files);
    void processPendingPaths();

    // 按清单逐条还原（添加文件 / 恢复会话 / 导入共用）
    enum class LoadPurpose { AddFiles, Restore, Import };
    void startLoading(const QList<StickerList::Entry>& entries, const QString& baseDir, LoadPurpose purpose);
    void onEntryLoaded(const ListLoader::Result& result, const QString& name);
    void onLoadingFinished(bool cancelled);

    // 会话
    void restoreSession();

    // 转换
    void startConversion(QList<int> rows);
    void connectConverter();

    // 导出
    void startExport(int mode);
    void connectExporter();

    // 截取 / 裁切
    void openEditor(int row);
    void openClipTool();
    void onRowAction(int row, int action);

    // 数据目录与预览缓存
    void applyDataDir();
    void refreshCacheInfo();
    void clearPreviewCacheClicked();

    // 右键菜单与关键词
    void showRowMenu(const QPoint& globalPos);
    void openKeywordDialog(const QList<int>& rows);

    // 列表操作
    void removeSelected();
    void clearList();
    void requestStop();
    void openOutputDir();
    void openDataDir();
    void notImplemented(const QString& feature);

    // 状态刷新
    void setBusy(Busy kind, int total);
    void refreshUi();
    void updateBadge();
    void updateLogSummary();
    void updateAiButton();
    QString busyTitle() const;
    QList<int> selectedRows() const;

    static QStringList scanFolder(const QString& dir);

    AppSettings m_settings;
    TaskListModel* m_model = nullptr;
    BatchConverter* m_converter = nullptr;
    Exporter* m_exporter = nullptr;
    ThumbnailService* m_thumbs = nullptr;
    ProxyCache* m_proxies = nullptr;
    SessionStore* m_session = nullptr;
    bool m_editorOpen = false;
    QHash<QString, QString> m_stageText; // taskId → 最近一次写入日志的阶段说明
    QString m_lastOutput;                // 最近一次生成的压缩包或文件夹

    HeaderBar* m_header = nullptr;
    QStackedWidget* m_listStack = nullptr;
    DropZone* m_dropZone = nullptr;
    TaskTableView* m_table = nullptr;
    SettingsPanel* m_panel = nullptr;
    LogPanel* m_log = nullptr;
    DropOverlay* m_overlay = nullptr;

    QPushButton* m_btnAddFiles = nullptr;
    QPushButton* m_btnClip = nullptr;
    QPushButton* m_btnAddFolder = nullptr;
    QPushButton* m_btnImport = nullptr;
    QPushButton* m_btnAi = nullptr;
    QPushButton* m_btnAiSettings = nullptr;
    QPushButton* m_btnClear = nullptr;

    Busy m_busy = Busy::None;
    int m_busyDone = 0;
    int m_busyTotal = 0;
    QString m_busyHint;
    LoadPurpose m_loadPurpose = LoadPurpose::AddFiles;
    int m_loadAdded = 0;
    int m_loadFailed = 0;
    int m_loadMissing = 0;
    int m_loadProxies = 0;
    bool m_restorePending = true; // 启动后会话恢复尚未开始
    QStringList m_pendingPaths;   // 恢复 / 导入期间收到的文件，结束后再添加
    std::shared_ptr<std::atomic_bool> m_cancel;
    QThreadPool m_workers;
    QString m_lastDir;
};

} // namespace tsm
