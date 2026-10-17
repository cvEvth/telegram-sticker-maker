#include "ui/editor/ClipListPanel.h"

#include "core/Format.h"
#include "ui/editor/TimelineWidgets.h"

#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace tsm {

namespace {
constexpr QSize kThumbSize(96, 54);

void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

// 只在内容变化且输入框没有焦点时回填，避免打断用户输入
void setTextIfIdle(QLineEdit* edit, const QString& text)
{
    if (!edit->hasFocus() && edit->text() != text)
        edit->setText(text);
}
} // namespace

// ---------------------------------------------------------------- ClipCard

ClipCard::ClipCard(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ClipCard"));
    setCursor(Qt::PointingHandCursor);

    m_index = new QLabel;
    m_index->setObjectName(QStringLiteral("ClipIndex"));
    m_index->setAlignment(Qt::AlignCenter);
    m_index->setFixedSize(22, 22);

    m_thumb = new QLabel;
    m_thumb->setObjectName(QStringLiteral("ClipThumb"));
    m_thumb->setFixedSize(kThumbSize);
    m_thumb->setAlignment(Qt::AlignCenter);

    m_start = new QLineEdit;
    m_start->setPlaceholderText(QStringLiteral("起点"));
    m_start->setToolTip(QStringLiteral("起点时间，支持 01:23.456 / 83.5 / 83.5s 等写法"));
    m_end = new QLineEdit;
    m_end->setPlaceholderText(QStringLiteral("终点"));
    m_end->setToolTip(QStringLiteral("终点时间；清空 = 未设终点"));
    m_emoji = new QLineEdit;
    m_emoji->setPlaceholderText(QStringLiteral("Emoji"));
    m_emoji->setMaximumWidth(76);
    for (QLineEdit* e : {m_start, m_end})
        e->setMinimumWidth(96);

    m_duration = new QLabel;
    m_duration->setObjectName(QStringLiteral("ClipDuration"));
    m_tags = new QLabel;
    m_tags->setObjectName(QStringLiteral("Hint"));
    m_tags->setWordWrap(true);

    m_menu = new QToolButton;
    m_menu->setText(QStringLiteral("⋮"));
    m_menu->setToolTip(QStringLiteral("更多操作"));

    // 第一行：序号 + 缩略图 + （Emoji、时长、⋮ / 标签）；第二行：起点 – 终点
    auto* meta = new QHBoxLayout;
    meta->setSpacing(6);
    meta->addWidget(m_emoji);
    meta->addWidget(m_duration);
    meta->addStretch();
    meta->addWidget(m_menu);

    auto* right = new QVBoxLayout;
    right->setSpacing(4);
    right->addLayout(meta);
    right->addWidget(m_tags);
    right->addStretch();

    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(m_index, 0, Qt::AlignTop);
    top->addWidget(m_thumb, 0, Qt::AlignTop);
    top->addLayout(right, 1);

    auto* times = new QHBoxLayout;
    times->setSpacing(4);
    times->addWidget(m_start, 1);
    times->addWidget(new QLabel(QStringLiteral("–")));
    times->addWidget(m_end, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addLayout(top);
    layout->addLayout(times);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    connect(m_start, &QLineEdit::editingFinished, this, [this] { emit startEdited(m_start->text()); });
    connect(m_end, &QLineEdit::editingFinished, this, [this] { emit endEdited(m_end->text()); });
    connect(m_emoji, &QLineEdit::editingFinished, this, [this] { emit emojiEdited(m_emoji->text()); });
    connect(m_menu, &QToolButton::clicked, this, [this] { emit menuRequested(m_menu->mapToGlobal(QPoint(0, m_menu->height()))); });
    // 点进输入框也算选中这个片段
    for (QLineEdit* e : {m_start, m_end, m_emoji})
        e->installEventFilter(this);
}

void ClipCard::setClip(int index, const StickerTask& clip, bool selected, PlayState play)
{
    m_index->setText(QString::number(index + 1));
    m_index->setStyleSheet(QStringLiteral("background:%1;").arg(clipColor(index).name()));
    setTextIfIdle(m_start, formatTimecode(clip.startTime.value_or(0)));
    setTextIfIdle(m_end, clip.endTime ? formatTimecode(*clip.endTime) : QString());
    setTextIfIdle(m_emoji, clip.emoji);

    if (clip.startTime && clip.endTime)
        m_duration->setText(formatSecondsShort(*clip.endTime - *clip.startTime) + QStringLiteral("s"));
    else
        m_duration->setText(QStringLiteral("未设终点"));
    m_duration->setProperty("missing", !clip.endTime.has_value());
    repolish(m_duration);

    QStringList tags;
    if (!clip.emoji.isEmpty())
        tags << clip.emoji;
    if (clip.mirror)
        tags << QStringLiteral("🪞 镜像");
    if (const QString crop = clip.cropTag(); !crop.isEmpty())
        tags << crop;
    if (play == PlayState::Playing)
        tags << QStringLiteral("▶️ 播放中");
    else if (play == PlayState::Looping)
        tags << QStringLiteral("🔁 循环中");
    m_tags->setText(tags.join(QStringLiteral("  ")));
    m_tags->setVisible(!tags.isEmpty());

    if (property("selected").toBool() != selected) {
        setProperty("selected", selected);
        repolish(this);
    }
}

void ClipCard::setThumbnail(const QImage& image)
{
    if (image.isNull()) {
        m_thumb->setPixmap({});
        return;
    }
    const qreal dpr = devicePixelRatioF();
    QPixmap pix = QPixmap::fromImage(image).scaled(kThumbSize * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pix.setDevicePixelRatio(dpr);
    m_thumb->setPixmap(pix);
}

bool ClipCard::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn && qobject_cast<QLineEdit*>(watched))
        emit clicked();
    return QFrame::eventFilter(watched, event);
}

void ClipCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    // 必须接受：否则事件会传给列表容器，被当成"点击空白"而立刻取消选中
    event->accept();
}

void ClipCard::contextMenuEvent(QContextMenuEvent* event)
{
    emit clicked();
    emit menuRequested(event->globalPos());
    event->accept();
}

// ---------------------------------------------------------------- ClipListPanel

ClipListPanel::ClipListPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ClipPanel"));
    setFixedWidth(360);

    auto* title = new QLabel(QStringLiteral("📋 待添加片段列表"));
    title->setObjectName(QStringLiteral("SectionTitle"));
    m_count = new QLabel;
    m_count->setObjectName(QStringLiteral("Hint"));
    auto* header = new QHBoxLayout;
    header->addWidget(title);
    header->addStretch();
    header->addWidget(m_count);

    m_listHost = new QWidget;
    m_listHost->installEventFilter(this);
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 4, 0);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();

    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_listHost);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->installEventFilter(this);

    auto* add = new QPushButton(QStringLiteral("➕ 添加片段"));
    add->setToolTip(QStringLiteral("从当前指针开始，终点留空，用 ] 打点"));
    add->setFocusPolicy(Qt::NoFocus);
    connect(add, &QPushButton::clicked, this, &ClipListPanel::addClicked);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addLayout(header);
    layout->addWidget(m_scroll, 1);
    layout->addWidget(add);
}

void ClipListPanel::setClips(const QList<StickerTask>& clips, int selected, int playingIndex, ClipCard::PlayState play)
{
    m_count->setText(QStringLiteral("共 %1 个片段").arg(clips.size()));
    // 可能正处在该卡片自己的信号里（⋮ 菜单删除片段），延迟销毁
    while (m_cards.size() > clips.size()) {
        ClipCard* card = m_cards.takeLast();
        card->hide();
        m_listLayout->removeWidget(card);
        card->deleteLater();
    }
    while (m_cards.size() < clips.size()) {
        auto* card = new ClipCard;
        const int index = int(m_cards.size());
        connect(card, &ClipCard::clicked, this, [this, index] { emit clipClicked(index); });
        connect(card, &ClipCard::startEdited, this, [this, index](const QString& t) { emit startEdited(index, t); });
        connect(card, &ClipCard::endEdited, this, [this, index](const QString& t) { emit endEdited(index, t); });
        connect(card, &ClipCard::emojiEdited, this, [this, index](const QString& t) { emit emojiEdited(index, t); });
        connect(card, &ClipCard::menuRequested, this, [this, index](const QPoint& p) { emit menuRequested(index, p); });
        m_listLayout->insertWidget(m_listLayout->count() - 1, card);
        m_cards << card;
    }
    for (int i = 0; i < clips.size(); ++i)
        m_cards.at(i)->setClip(i, clips.at(i), i == selected, i == playingIndex ? play : ClipCard::PlayState::None);
}

void ClipListPanel::setThumbnail(int index, const QImage& image)
{
    if (index >= 0 && index < m_cards.size())
        m_cards.at(index)->setThumbnail(image);
}

void ClipListPanel::scrollToClip(int index)
{
    if (index >= 0 && index < m_cards.size())
        m_scroll->ensureWidgetVisible(m_cards.at(index), 0, 8);
}

bool ClipListPanel::eventFilter(QObject* watched, QEvent* event)
{
    // 点击列表空白 = 取消选中片段
    if ((watched == m_listHost || watched == m_scroll->viewport()) && event->type() == QEvent::MouseButtonPress)
        emit backgroundClicked();
    return QFrame::eventFilter(watched, event);
}

} // namespace tsm
