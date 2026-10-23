#pragma once

#include "ui/Theme.h"

#include <QFrame>

class QAction;
class QLabel;
class QPushButton;

namespace tsm {

// 顶部标题栏：图标 + 标题 + 副标题 + 右侧网站链接、状态徽章、主题切换
class HeaderBar : public QFrame {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);
    void setBadge(const QString& text, bool muted);
    // 显示"发现新版本"按钮，点击打开 pageUrl
    void showUpdate(const QString& version, const QString& pageUrl);
    void setThemeMode(ThemeMode mode);

signals:
    void themeModeChosen(ThemeMode mode);

private:
    QLabel* m_badge = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_theme = nullptr;
    QList<QAction*> m_themeActions; // 与 ThemeMode 顺序一致
    QString m_updateUrl;
};

} // namespace tsm
