#include "ui/LogPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace tsm {

LogPanel::LogPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("LogPanel"));

    m_toggle = new QToolButton;
    m_toggle->setObjectName(QStringLiteral("LogToggle"));
    m_toggle->setText(QStringLiteral("📋 运行日志"));
    m_toggle->setCursor(Qt::PointingHandCursor);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("LogSummary"));

    auto* clear = new QPushButton(QStringLiteral("清空日志"));
    clear->setObjectName(QStringLiteral("SmallButton"));

    m_arrow = new QToolButton;
    m_arrow->setArrowType(Qt::UpArrow);
    m_arrow->setCursor(Qt::PointingHandCursor);

    auto* header = new QWidget;
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 4, 12, 4);
    headerLayout->addWidget(m_toggle);
    headerLayout->addWidget(m_summary, 1);
    headerLayout->addWidget(clear);
    headerLayout->addWidget(m_arrow);

    m_view = new QPlainTextEdit;
    m_view->setObjectName(QStringLiteral("LogView"));
    m_view->setReadOnly(true);
    m_view->setPlaceholderText(QStringLiteral("暂无输出日志..."));
    m_view->setMaximumBlockCount(5000);
    m_view->setFixedHeight(180);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(9);
    m_view->setFont(mono);
    m_view->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_view);

    connect(m_toggle, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    connect(m_arrow, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    connect(clear, &QPushButton::clicked, m_view, &QPlainTextEdit::clear);
}

void LogPanel::setSummary(const QString& summary)
{
    m_summary->setText(summary);
}

void LogPanel::appendLine(const QString& line)
{
    m_view->appendPlainText(line);
    m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
}

void LogPanel::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_view->setVisible(expanded);
    m_arrow->setArrowType(expanded ? Qt::DownArrow : Qt::UpArrow);
}

} // namespace tsm
