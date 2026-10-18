#pragma once

#include <QFrame>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace tsm {

class AppSettings;

// 右侧设置与操作面板。所有选项修改后立即写入 AppSettings。
class SettingsPanel : public QFrame {
    Q_OBJECT

public:
    enum ExportMode { ExportSources = 0, ExportTrimmed = 1 };

    struct BusyState {
        bool active = false;
        bool converting = false;
        QString title;
        int done = 0;
        int total = 0;
        QString hint;
    };

    explicit SettingsPanel(AppSettings& settings, QWidget* parent = nullptr);

    void setBusyState(const BusyState& state);
    void setCacheInfo(const QString& text);

signals:
    void startRequested();
    void exportRequested(int mode);
    void stopRequested();
    void openOutputRequested();
    void outputLocationChanged();
    void dataDirChanged(const QString& oldDir, const QString& newDir);
    void openDataDirRequested();
    void clearCacheRequested();

private:
    void applyOutputDir(const QString& dir);
    void applyDataDirChoice(const QString& choice);
    void refreshExportMenu();
    void refreshOutputDirRow();
    void refreshDataDirLabel();

    AppSettings& m_s;

    QPushButton* m_modeSticker = nullptr;
    QPushButton* m_modeEmoji = nullptr;
    QComboBox* m_preset = nullptr;
    QCheckBox* m_besideSource = nullptr;
    QWidget* m_outputRow = nullptr;
    QLineEdit* m_outputDir = nullptr;
    QPushButton* m_browseOutput = nullptr;
    QLineEdit* m_dataDir = nullptr;
    QPushButton* m_browseData = nullptr;
    QLabel* m_dataResolved = nullptr;
    QPushButton* m_clearCache = nullptr;
    QLabel* m_cacheInfo = nullptr;
    QComboBox* m_language = nullptr;
    QLabel* m_languageHint = nullptr;

    QFrame* m_progressCard = nullptr;
    QLabel* m_progressTitle = nullptr;
    QLabel* m_progressCount = nullptr;
    QLabel* m_progressHint = nullptr;
    QProgressBar* m_progressBar = nullptr;

    QPushButton* m_start = nullptr;
    QPushButton* m_export = nullptr;
    QAction* m_exportSources = nullptr;
    QAction* m_exportTrimmed = nullptr;
    QCheckBox* m_zip = nullptr;
    QPushButton* m_stop = nullptr;
    QPushButton* m_openOutput = nullptr;
};

} // namespace tsm
