#pragma once

#include "app/AppSettings.h"
#include "media/MediaProbe.h"

#include <QMainWindow>
#include <QThreadPool>
#include <atomic>
#include <memory>

class QPushButton;
class QStackedWidget;
class QTableView;

namespace tsm {

class BatchConverter;
class DropOverlay;
class DropZone;
class HeaderBar;
class LogPanel;
class SettingsPanel;
class TaskListModel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

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
    void startProbing(const QStringList& files);
    void onProbed(const QString& path, const ProbeResult& result);
    void onProbingFinished(bool cancelled);

    // 转换
    void startConversion(QList<int> rows);
    void connectConverter();

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
    static QString findManifestIn(const QString& dir);

    AppSettings m_settings;
    TaskListModel* m_model = nullptr;
    BatchConverter* m_converter = nullptr;
    QHash<QString, QString> m_stageText; // taskId → 最近一次写入日志的阶段说明
    QString m_lastOutput;                // 最近一次生成的压缩包或文件夹

    HeaderBar* m_header = nullptr;
    QStackedWidget* m_listStack = nullptr;
    DropZone* m_dropZone = nullptr;
    QTableView* m_table = nullptr;
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
    int m_probeAdded = 0;
    int m_probeFailed = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QThreadPool m_workers;
    QString m_lastDir;
};

} // namespace tsm
