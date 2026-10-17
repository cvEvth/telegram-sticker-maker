#include "ui/KeywordDialog.h"

#include "core/Keywords.h"
#include "core/Spec.h"
#include "ui/FlowLayout.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace tsm {

namespace {
void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}
} // namespace

KeywordDialog::KeywordDialog(const QString& subtitle, const QStringList& initial, QWidget* parent)
    : QDialog(parent)
    , m_keywords(initial)
{
    setWindowTitle(QStringLiteral("编辑关键词"));
    setObjectName(QStringLiteral("KeywordDialog"));
    setMinimumWidth(460);

    auto* title = new QLabel(QStringLiteral("编辑关键词"));
    title->setObjectName(QStringLiteral("DialogTitle"));
    auto* sub = new QLabel(subtitle);
    sub->setObjectName(QStringLiteral("Hint"));
    auto* desc = new QLabel(QStringLiteral("逐个添加搜索关键词，最多 %1 个，合计不超过 %2 字符。仅写入压缩包内的 stickers.json。")
                                .arg(spec::kMaxKeywords)
                                .arg(spec::kMaxKeywordChars));
    desc->setObjectName(QStringLiteral("Hint"));
    desc->setWordWrap(true);

    m_chipHost = new QFrame;
    m_chipHost->setObjectName(QStringLiteral("ChipBox"));
    m_chipHost->setMinimumHeight(72);
    m_chips = new FlowLayout(m_chipHost, 6);
    m_chips->setContentsMargins(10, 10, 10, 10);
    m_empty = new QLabel(QStringLiteral("还没有关键词"));
    m_empty->setObjectName(QStringLiteral("Hint"));

    m_input = new QLineEdit;
    m_input->setPlaceholderText(QStringLiteral("输入关键词，回车添加（可用逗号、顿号、空格分隔多个）"));
    m_input->installEventFilter(this);
    m_add = new QPushButton(QStringLiteral("添加"));
    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_add);

    m_counter = new QLabel;
    m_counter->setObjectName(QStringLiteral("Counter"));
    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("Hint"));
    m_message->setProperty("warning", true);
    auto* status = new QHBoxLayout;
    status->addWidget(m_message, 1);
    status->addWidget(m_counter);

    auto* cancel = new QPushButton(QStringLiteral("取消"));
    auto* save = new QPushButton(QStringLiteral("保存"));
    save->setObjectName(QStringLiteral("PrimaryButton"));
    save->setMinimumSize(96, 34);
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
    layout->addWidget(m_chipHost);
    layout->addLayout(inputRow);
    layout->addLayout(status);
    layout->addSpacing(6);
    layout->addLayout(buttons);

    connect(m_input, &QLineEdit::returnPressed, this, &KeywordDialog::addFromInput);
    connect(m_add, &QPushButton::clicked, this, &KeywordDialog::addFromInput);
    connect(m_input, &QLineEdit::textEdited, this, [this] { showMessage({}); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        // 输入框里还有没提交的词，保存时一并加入
        if (!m_input->text().trimmed().isEmpty()) {
            addFromInput();
            if (!m_message->text().isEmpty())
                return;
        }
        accept();
    });

    rebuildChips();
    refreshState();
    m_input->setFocus();
}

void KeywordDialog::addFromInput()
{
    const QString text = m_input->text();
    if (text.trimmed().isEmpty())
        return;
    const Keywords::AddResult result = Keywords::add(m_keywords, text);
    showMessage(result.error);
    m_input->clear();
    if (!result.added.isEmpty())
        rebuildChips();
    refreshState();
}

void KeywordDialog::removeAt(int index)
{
    if (index < 0 || index >= m_keywords.size())
        return;
    m_keywords.removeAt(index);
    showMessage({});
    rebuildChips();
    refreshState();
    m_input->setFocus();
}

void KeywordDialog::rebuildChips()
{
    while (QLayoutItem* item = m_chips->takeAt(0)) {
        if (QWidget* w = item->widget(); w && w != m_empty)
            w->deleteLater();
        delete item;
    }
    m_empty->setVisible(m_keywords.isEmpty());
    if (m_keywords.isEmpty()) {
        m_chips->addWidget(m_empty);
        return;
    }
    for (int i = 0; i < m_keywords.size(); ++i) {
        auto* chip = new QFrame;
        chip->setObjectName(QStringLiteral("Chip"));
        auto* label = new QLabel(m_keywords.at(i));
        auto* close = new QToolButton;
        close->setObjectName(QStringLiteral("ChipClose"));
        close->setText(QStringLiteral("✕"));
        close->setToolTip(QStringLiteral("删除"));
        close->setCursor(Qt::PointingHandCursor);
        auto* row = new QHBoxLayout(chip);
        row->setContentsMargins(10, 2, 4, 2);
        row->setSpacing(2);
        row->addWidget(label);
        row->addWidget(close);
        connect(close, &QToolButton::clicked, this, [this, i] { removeAt(i); });
        m_chips->addWidget(chip);
    }
    m_chipHost->updateGeometry();
}

void KeywordDialog::refreshState()
{
    const int count = int(m_keywords.size());
    const int chars = Keywords::totalChars(m_keywords);
    m_counter->setText(QStringLiteral("%1/%2 · %3/%4").arg(count).arg(spec::kMaxKeywords).arg(chars).arg(spec::kMaxKeywordChars));
    const bool full = Keywords::isFull(m_keywords);
    if (m_counter->property("full").toBool() != full) {
        m_counter->setProperty("full", full); // 到上限变黄
        repolish(m_counter);
    }
    m_input->setEnabled(!full);
    m_add->setEnabled(!full);
    m_input->setPlaceholderText(full ? QStringLiteral("已达上限") : QStringLiteral("输入关键词，回车添加（可用逗号、顿号、空格分隔多个）"));
    if (!full)
        m_input->setFocus();
}

void KeywordDialog::showMessage(const QString& text)
{
    m_message->setText(text);
}

bool KeywordDialog::eventFilter(QObject* watched, QEvent* event)
{
    // 输入框为空时按 Backspace 删除最后一个词
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Backspace && m_input->text().isEmpty() && !m_keywords.isEmpty()) {
            removeAt(int(m_keywords.size()) - 1);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void KeywordDialog::keyPressEvent(QKeyEvent* event)
{
    // 回车只用于添加关键词，不触发保存；Esc 关闭（QDialog 默认行为）
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace tsm
