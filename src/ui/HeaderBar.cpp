#include "ui/HeaderBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

namespace tsm {

HeaderBar::HeaderBar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("HeaderBar"));

    auto* title = new QLabel(QStringLiteral("✨ Telegram Sticker Maker"));
    title->setObjectName(QStringLiteral("AppTitle"));
    auto* subtitle = new QLabel(QStringLiteral("Telegram 贴纸批量转换工具 | 支持 WebM 视频贴纸与 WebP 静态贴纸"));
    subtitle->setObjectName(QStringLiteral("AppSubtitle"));

    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(title);
    texts->addWidget(subtitle);

    m_badge = new QLabel;
    m_badge->setObjectName(QStringLiteral("Badge"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->addLayout(texts);
    layout->addStretch();
    layout->addWidget(m_badge, 0, Qt::AlignVCenter);
}

void HeaderBar::setBadge(const QString& text, bool muted)
{
    m_badge->setText(text);
    if (m_badge->property("muted").toBool() != muted) {
        m_badge->setProperty("muted", muted);
        m_badge->style()->unpolish(m_badge);
        m_badge->style()->polish(m_badge);
    }
}

} // namespace tsm
