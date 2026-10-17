#pragma once

#include <QFrame>

class QLabel;
class QPlainTextEdit;
class QToolButton;

namespace tsm {

// 底部可折叠运行日志
class LogPanel : public QFrame {
    Q_OBJECT

public:
    explicit LogPanel(QWidget* parent = nullptr);

    void setSummary(const QString& summary);
    void appendLine(const QString& line);
    void setExpanded(bool expanded);

private:
    QToolButton* m_toggle = nullptr;
    QToolButton* m_arrow = nullptr;
    QLabel* m_summary = nullptr;
    QPlainTextEdit* m_view = nullptr;
    bool m_expanded = false;
};

} // namespace tsm
