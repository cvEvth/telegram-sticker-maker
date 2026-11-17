#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

namespace tsm {

class AppSettings;

// 顶部标题栏：图标 + 标题 + 副标题 + 右侧网站链接、状态徽章、主题按钮、语言按钮
class HeaderBar : public QFrame {
    Q_OBJECT

public:
    explicit HeaderBar(AppSettings& settings, QWidget* parent = nullptr);
    void setBadge(const QString& text, bool muted);
    // 显示"发现新版本"按钮，点击打开 pageUrl
    void showUpdate(const QString& version, const QString& pageUrl);

private:
    void updateThemeIcon(const QString& mode);

    QLabel* m_badge = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_theme = nullptr;
    QPushButton* m_language = nullptr;
    QString m_updateUrl;
};

} // namespace tsm
