#pragma once

#include <QFrame>

class QLabel;
class QProgressBar;
class QStackedWidget;

namespace tsm {

// 空列表时的大号拖放区；导入 / 恢复进行中时改为显示进度
class DropZone : public QFrame {
    Q_OBJECT

public:
    explicit DropZone(QWidget* parent = nullptr);

    void showIdle();
    void showProgress(const QString& title, const QString& hint, int done, int total);

signals:
    void addFilesClicked();
    void addFolderClicked();
    void importClicked();

private:
    QStackedWidget* m_stack = nullptr;
    QLabel* m_progressTitle = nullptr;
    QLabel* m_progressHint = nullptr;
    QLabel* m_progressCount = nullptr;
    QProgressBar* m_progressBar = nullptr;
};

} // namespace tsm
