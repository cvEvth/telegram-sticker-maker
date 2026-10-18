#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

namespace tsm {

// 顶部标题栏：标题 + 副标题 + 右侧状态徽章
class HeaderBar : public QFrame {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);
    void setBadge(const QString& text, bool muted);
    // 显示"发现新版本"按钮，点击打开 pageUrl
    void showUpdate(const QString& version, const QString& pageUrl);

private:
    QLabel* m_badge = nullptr;
    QPushButton* m_update = nullptr;
    QString m_updateUrl;
};

} // namespace tsm
