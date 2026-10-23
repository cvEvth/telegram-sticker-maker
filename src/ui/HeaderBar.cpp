#include "ui/HeaderBar.h"

#include "app/AppSettings.h"
#include "app/Language.h"
#include "ui/BotPromo.h"
#include "ui/Theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QDesktopServices>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

namespace tsm {

HeaderBar::HeaderBar(AppSettings& settings, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("HeaderBar"));

    auto* logo = new QLabel;
    logo->setPixmap(QIcon(QStringLiteral(":/icons/app.ico")).pixmap(36, 36));
    auto* title = new QLabel(QStringLiteral("Telegram Sticker Maker"));
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

    // 程序偏好（与转换无关的设置）：主题立即生效，语言重启后生效
    m_prefs = new QPushButton(QStringLiteral("⚙"));
    m_prefs->setObjectName(QStringLiteral("PrefsButton"));
    m_prefs->setCursor(Qt::PointingHandCursor);
    m_prefs->setToolTip(tr("设置"));
    auto* menu = new QMenu(m_prefs);
    menu->setToolTipsVisible(true);
    const auto addHeading = [menu](const QString& text) {
        QAction* heading = menu->addAction(text);
        heading->setEnabled(false);
    };
    // 一组单选项；current 为当前值，选中后调用 apply
    const auto addChoices = [this, menu](const QList<std::pair<QString, QString>>& choices, const QString& current,
                                         const std::function<void(const QString&)>& apply) {
        auto* group = new QActionGroup(menu);
        for (const auto& [value, text] : choices) {
            QAction* action = menu->addAction(text);
            action->setCheckable(true);
            action->setChecked(value == current);
            action->setData(value);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [apply, value] { apply(value); });
        }
    };

    addHeading(tr("界面主题"));
    addChoices({{QStringLiteral("system"), tr("跟随系统")},
                {QStringLiteral("light"), tr("浅色")},
                {QStringLiteral("dark"), tr("深色")}},
               themeModeToString(themeModeFromString(settings.theme())), [&settings](const QString& value) {
                   settings.setTheme(value);
                   applyTheme(*qApp, themeModeFromString(value));
               });

    menu->addSeparator();
    addHeading(tr("界面语言"));
    QList<std::pair<QString, QString>> languages{{QString(), tr("跟随系统")}};
    for (const Language::Option& option : Language::available())
        languages.append({option.code, option.nativeName});
    addChoices(languages, settings.language(), [this, &settings](const QString& code) {
        if (code == settings.language())
            return;
        settings.setLanguage(code);
        QMessageBox::information(window(), tr("界面语言"), tr("重启程序后生效"));
    });

    menu->addSeparator();
    QAction* updates = menu->addAction(tr("启动时检查新版本"));
    updates->setCheckable(true);
    updates->setChecked(settings.checkUpdates());
    updates->setToolTip(tr("每天最多联网查询一次 GitHub 上的最新版本，只提醒，不会自动下载。"));
    connect(updates, &QAction::toggled, this, [&settings](bool on) { settings.setCheckUpdates(on); });
    m_prefs->setMenu(menu);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 10, 18, 10);
    layout->setSpacing(12);
    layout->addWidget(logo, 0, Qt::AlignVCenter);
    layout->addLayout(texts);
    layout->addStretch();
    layout->addWidget(m_update, 0, Qt::AlignVCenter);
    layout->addSpacing(10);
    layout->addWidget(site, 0, Qt::AlignVCenter);
    layout->addSpacing(10);
    layout->addWidget(m_badge, 0, Qt::AlignVCenter);
    layout->addSpacing(4);
    layout->addWidget(m_prefs, 0, Qt::AlignVCenter);
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
