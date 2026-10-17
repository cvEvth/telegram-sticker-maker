#include "ui/ConversionDoneDialog.h"

#include "ui/BotPromo.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace tsm {

ConversionDoneDialog::ConversionDoneDialog(int succeeded, int failed, const QString& location, bool zipOutput, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("转换成功"));
    setObjectName(QStringLiteral("ConversionDoneDialog"));
    setMinimumWidth(540);

    auto* title = new QLabel(QStringLiteral("🎉 转换成功"));
    title->setObjectName(QStringLiteral("DialogTitle"));
    const QString counts = failed > 0 ? QStringLiteral("%1 个贴纸转换成功，%2 个失败（详见日志）").arg(succeeded).arg(failed)
                                      : QStringLiteral("%1 个贴纸已全部转换成功").arg(succeeded);
    auto* summary = new QLabel(counts);
    auto* where = new QLabel(QStringLiteral("%1：%2").arg(zipOutput ? QStringLiteral("压缩包") : QStringLiteral("保存位置"),
                                                          QDir::toNativeSeparators(location)));
    where->setObjectName(QStringLiteral("Hint"));
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // 引导去网站创建贴纸包
    auto* promo = new QFrame;
    promo->setObjectName(QStringLiteral("SitePromo"));
    auto* promoTitle = new QLabel(QStringLiteral("下一步：创建 Telegram 贴纸包"));
    promoTitle->setObjectName(QStringLiteral("SectionTitle"));
    auto* promoText = new QLabel(
        QStringLiteral("前往 <a href=\"%1\" style=\"color:#5ec2ff; text-decoration:none;\"><b>www.cvevth.top/sticker</b></a>"
                       " 创建你的 Telegram 贴纸包")
            .arg(BotPromo::galleryUrl()));
    promoText->setObjectName(QStringLiteral("SitePromoText"));
    promoText->setWordWrap(true);
    promoText->setTextFormat(Qt::RichText);
    promoText->setTextInteractionFlags(Qt::TextBrowserInteraction);
    promoText->setOpenExternalLinks(true);
    auto* botHint = new QLabel(zipOutput ? QStringLiteral("也可以把压缩包直接发给 Telegram 机器人 %1。").arg(BotPromo::handle())
                                         : QStringLiteral("勾选「输出为压缩包」后，也可以把压缩包直接发给 Telegram 机器人 %1。")
                                               .arg(BotPromo::handle()));
    botHint->setObjectName(QStringLiteral("Hint"));
    botHint->setWordWrap(true);
    auto* promoLayout = new QVBoxLayout(promo);
    promoLayout->setContentsMargins(14, 12, 14, 12);
    promoLayout->setSpacing(6);
    promoLayout->addWidget(promoTitle);
    promoLayout->addWidget(promoText);
    promoLayout->addWidget(botHint);

    m_dontShow = new QCheckBox(QStringLiteral("以后不再弹出"));
    auto* site = new QPushButton(QStringLiteral("🌐 去网站创建"));
    site->setObjectName(QStringLiteral("PrimaryButton"));
    site->setMinimumHeight(36);
    site->setDefault(true);
    auto* open = new QPushButton(QStringLiteral("📂 打开输出目录"));
    auto* close = new QPushButton(QStringLiteral("关闭"));
    for (QPushButton* b : {site, open, close})
        b->setCursor(Qt::PointingHandCursor);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_dontShow);
    buttons->addStretch();
    buttons->addWidget(close);
    buttons->addWidget(open);
    buttons->addWidget(site);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addWidget(where);
    layout->addSpacing(4);
    layout->addWidget(promo);
    layout->addSpacing(4);
    layout->addLayout(buttons);

    connect(site, &QPushButton::clicked, this, [this] {
        BotPromo::openGallery();
        accept();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        emit openOutputRequested();
        accept();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
}

bool ConversionDoneDialog::dontShowAgain() const
{
    return m_dontShow->isChecked();
}

} // namespace tsm
