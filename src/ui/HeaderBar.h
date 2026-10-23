#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

namespace tsm {

class AppSettings;

// 顶部标题栏：图标 + 标题 + 副标题 + 右侧网站链接、状态徽章、设置菜单（主题 / 语言 / 检查更新）
class HeaderBar : public QFrame {
    Q_OBJECT

public:
    explicit HeaderBar(AppSettings& settings, QWidget* parent = nullptr);
    void setBadge(const QString& text, bool muted);
    // 显示"发现新版本"按钮，点击打开 pageUrl
    void showUpdate(const QString& version, const QString& pageUrl);

private:
    QLabel* m_badge = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_prefs = nullptr;
    QString m_updateUrl;
};

} // namespace tsm
