#include "ui/HeaderBar.h"

#include "ui/BotPromo.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace tsm {

HeaderBar::HeaderBar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("HeaderBar"));

    auto* title = new QLabel(QStringLiteral("✨ Telegram Sticker Maker"));
    title->setObjectName(QStringLiteral("AppTitle"));
    auto* subtitle = new QLabel(tr("Telegram 贴纸批量转换工具 | 支持 WebM 视频贴纸与 WebP 静态贴纸"));
    subtitle->setObjectName(QStringLiteral("AppSubtitle"));

    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(title);
    texts->addWidget(subtitle);

    m_badge = new QLabel;
    m_badge->setObjectName(QStringLiteral("Badge"));

    // 配合使用的网站：在线创建 Telegram 贴纸包
    auto* site = new QPushButton(tr("🌐 cvevth.top 在线创建贴纸包"));
    site->setObjectName(QStringLiteral("SiteLink"));
    site->setCursor(Qt::PointingHandCursor);
    site->setToolTip(tr("前往 %1 创建 Telegram 贴纸包\n也可以把转换好的压缩包直接发给 %2")
                         .arg(BotPromo::galleryUrl(), BotPromo::handle()));
    connect(site, &QPushButton::clicked, this, &BotPromo::openGallery);

    // 有新版本时才显示
    m_update = new QPushButton;
    m_update->setObjectName(QStringLiteral("UpdateLink"));
    m_update->setCursor(Qt::PointingHandCursor);
    m_update->hide();
    connect(m_update, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_updateUrl)); });

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->addLayout(texts);
    layout->addStretch();
    layout->addWidget(m_update, 0, Qt::AlignVCenter);
    layout->addSpacing(10);
    layout->addWidget(site, 0, Qt::AlignVCenter);
    layout->addSpacing(10);
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

void HeaderBar::showUpdate(const QString& version, const QString& pageUrl)
{
    m_updateUrl = pageUrl;
    m_update->setText(tr("⬆️ 新版本 %1").arg(version));
    m_update->setToolTip(tr("点击前往下载页面"));
    m_update->show();
}

} // namespace tsm
