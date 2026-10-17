#include "ui/EmojiDialog.h"

#include "core/AiEmoji.h"
#include "ui/FlowLayout.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace tsm {

namespace {
// 常用的表情 / 反应类 Emoji
const QStringList& quickPicks()
{
    static const QStringList list{
        QStringLiteral("😂"), QStringLiteral("🤣"), QStringLiteral("😭"), QStringLiteral("🥺"), QStringLiteral("😡"),
        QStringLiteral("😱"), QStringLiteral("😴"), QStringLiteral("🤔"), QStringLiteral("😎"), QStringLiteral("🥰"),
        QStringLiteral("😍"), QStringLiteral("😘"), QStringLiteral("😳"), QStringLiteral("🙄"), QStringLiteral("😏"),
        QStringLiteral("👍"), QStringLiteral("👋"), QStringLiteral("🙏"), QStringLiteral("👏"), QStringLiteral("❤️"),
        QStringLiteral("🎉"), QStringLiteral("💢"), QStringLiteral("💤"), QStringLiteral("❓"),
    };
    return list;
}
} // namespace

EmojiDialog::EmojiDialog(const QString& subtitle, const QString& initial, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置 Emoji"));
    setObjectName(QStringLiteral("EmojiDialog"));
    setMinimumWidth(440);

    auto* title = new QLabel(QStringLiteral("设置 Emoji"));
    title->setObjectName(QStringLiteral("DialogTitle"));
    auto* sub = new QLabel(subtitle);
    sub->setObjectName(QStringLiteral("Hint"));
    auto* desc = new QLabel(QStringLiteral("覆盖选中贴纸的 Emoji，文件名随之更新。只保留 Emoji 字符，最多 %1 个字符；留空则清除。")
                                .arg(AiEmoji::kMaxEmojiChars));
    desc->setObjectName(QStringLiteral("Hint"));
    desc->setWordWrap(true);

    auto* picks = new QWidget;
    auto* flow = new FlowLayout(picks, 6);
    flow->setContentsMargins(0, 0, 0, 0);
    for (const QString& e : quickPicks()) {
        auto* b = new QPushButton(e);
        b->setObjectName(QStringLiteral("EmojiPick"));
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(36, 32);
        connect(b, &QPushButton::clicked, this, [this, e] { append(e); });
        flow->addWidget(b);
    }

    m_input = new QLineEdit(initial);
    m_input->setPlaceholderText(QStringLiteral("输入或点选 Emoji，如 😂🤣"));
    m_input->setClearButtonEnabled(true);
    m_preview = new QLabel;
    m_preview->setObjectName(QStringLiteral("Hint"));

    auto* cancel = new QPushButton(QStringLiteral("取消"));
    auto* save = new QPushButton(QStringLiteral("保存"));
    save->setObjectName(QStringLiteral("PrimaryButton"));
    save->setMinimumSize(96, 34);
    save->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(save);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(sub);
    layout->addWidget(desc);
    layout->addWidget(picks);
    layout->addWidget(m_input);
    layout->addWidget(m_preview);
    layout->addLayout(buttons);

    connect(m_input, &QLineEdit::textChanged, this, &EmojiDialog::refreshPreview);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    refreshPreview();
    m_input->setFocus();
    m_input->selectAll();
}

QString EmojiDialog::emoji() const
{
    return AiEmoji::extractEmoji(m_input->text(), false);
}

void EmojiDialog::append(const QString& e)
{
    const QString next = emoji() + e;
    if (AiEmoji::extractEmoji(next, false) != next) {
        m_preview->setProperty("warning", true);
        m_preview->setText(QStringLiteral("最多 %1 个字符，放不下了").arg(AiEmoji::kMaxEmojiChars));
        m_preview->style()->polish(m_preview);
        return;
    }
    m_input->setText(next);
    m_input->setFocus();
}

void EmojiDialog::refreshPreview()
{
    const QString text = m_input->text().trimmed();
    const QString result = emoji();
    QString message;
    bool warning = false;
    if (text.isEmpty()) {
        message = QStringLiteral("将清除选中贴纸的 Emoji");
    } else if (result.isEmpty()) {
        message = QStringLiteral("没有识别到 Emoji，保存后将清除");
        warning = true;
    } else {
        QString compact = text;
        compact.remove(u' ');
        message = QStringLiteral("将设置为：%1").arg(result);
        if (compact != result) {
            message += QStringLiteral("（已忽略非 Emoji 文字或超出部分）");
            warning = true;
        }
    }
    m_preview->setProperty("warning", warning);
    m_preview->setText(message);
    m_preview->style()->polish(m_preview);
}

} // namespace tsm
