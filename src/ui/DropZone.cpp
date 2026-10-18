#include "ui/DropZone.h"

#include "ui/BotPromo.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace tsm {

namespace {
QLabel* centeredLabel(const QString& text, const QString& objectName)
{
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignCenter);
    return label;
}
} // namespace

DropZone::DropZone(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("DropZone"));

    // 空闲页
    auto* idle = new QWidget;
    auto* idleLayout = new QVBoxLayout(idle);
    idleLayout->setSpacing(12);
    idleLayout->addStretch();
    idleLayout->addWidget(centeredLabel(QStringLiteral("📂"), QStringLiteral("DropIcon")));
    idleLayout->addWidget(centeredLabel(tr("拖拽图片或视频到这里"), QStringLiteral("DropTitle")));
    idleLayout->addWidget(centeredLabel(tr("支持多文件与文件夹拖拽，也可导入已导出的 zip / JSON"),
                                        QStringLiteral("DropHint")));

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    auto* addFiles = new QPushButton(tr("➕ 添加文件"));
    auto* addFolder = new QPushButton(tr("📁 添加文件夹"));
    auto* import = new QPushButton(tr("📥 导入"));
    for (auto* button : {addFiles, addFolder, import}) {
        button->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(button);
    }
    buttons->addStretch();
    idleLayout->addSpacing(6);
    idleLayout->addLayout(buttons);

    auto* tags = new QHBoxLayout;
    tags->setSpacing(6);
    tags->addStretch();
    for (const char* format : {"MP4", "MKV", "WebM", "MOV", "PNG", "JPG", "WebP", "GIF", "HEIC", "APNG"}) {
        auto* tag = new QLabel(QString::fromLatin1(format));
        tag->setObjectName(QStringLiteral("FormatTag"));
        tags->addWidget(tag);
    }
    tags->addStretch();
    idleLayout->addSpacing(4);
    idleLayout->addLayout(tags);
    idleLayout->addStretch();

    // 配合使用的机器人与贴纸库
    auto* promo = centeredLabel(
        tr("做好的贴纸可前往 <a href=\"%1\" style=\"color:#5ec2ff; text-decoration:none;\">www.cvevth.top/sticker</a>"
           " 创建 Telegram 贴纸包 · 也可把压缩包发给 <a href=\"%2\" style=\"color:#5ec2ff; text-decoration:none;\">%3</a>")
            .arg(BotPromo::galleryUrl(), BotPromo::botUrl(), BotPromo::handle()),
        QStringLiteral("PromoLine"));
    promo->setTextFormat(Qt::RichText);
    promo->setTextInteractionFlags(Qt::TextBrowserInteraction);
    promo->setOpenExternalLinks(true);
    idleLayout->addWidget(promo);

    // 进度页
    auto* progress = new QWidget;
    auto* progressLayout = new QVBoxLayout(progress);
    progressLayout->setSpacing(10);
    progressLayout->addStretch();
    m_progressTitle = centeredLabel(QString(), QStringLiteral("DropTitle"));
    m_progressHint = centeredLabel(QString(), QStringLiteral("DropHint"));
    m_progressBar = new QProgressBar;
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedSize(360, 8);
    m_progressCount = centeredLabel(QString(), QStringLiteral("DropHint"));
    progressLayout->addWidget(m_progressTitle);
    progressLayout->addWidget(m_progressHint);
    progressLayout->addWidget(m_progressBar, 0, Qt::AlignHCenter);
    progressLayout->addWidget(m_progressCount);
    progressLayout->addStretch();

    m_stack = new QStackedWidget;
    m_stack->addWidget(idle);
    m_stack->addWidget(progress);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_stack);

    connect(addFiles, &QPushButton::clicked, this, &DropZone::addFilesClicked);
    connect(addFolder, &QPushButton::clicked, this, &DropZone::addFolderClicked);
    connect(import, &QPushButton::clicked, this, &DropZone::importClicked);
}

void DropZone::showIdle()
{
    m_stack->setCurrentIndex(0);
}

void DropZone::showProgress(const QString& title, const QString& hint, int done, int total)
{
    m_progressTitle->setText(title);
    m_progressHint->setText(hint);
    m_progressBar->setRange(0, qMax(1, total));
    m_progressBar->setValue(done);
    m_progressCount->setText(QStringLiteral("%1 / %2").arg(done).arg(total));
    m_stack->setCurrentIndex(1);
}

} // namespace tsm
