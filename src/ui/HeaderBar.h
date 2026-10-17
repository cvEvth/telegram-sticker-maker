#pragma once

#include <QFrame>

class QLabel;

namespace tsm {

// 顶部标题栏：标题 + 副标题 + 右侧状态徽章
class HeaderBar : public QFrame {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);
    void setBadge(const QString& text, bool muted);

private:
    QLabel* m_badge = nullptr;
};

} // namespace tsm
