#include "ui/TaskTableDelegates.h"

#include "core/Format.h"
#include "core/TaskListModel.h"
#include "media/ThumbnailService.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace tsm {

namespace {
constexpr QSize kThumbBox(72, 54);
constexpr int kActionWidth = 28;

// 只画单元格背景（选中 / 悬停），不画模型里的文字
void paintBackground(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    QStyleOptionViewItem opt = option;
    opt.index = index;
    opt.text.clear();
    opt.icon = {};
    opt.features &= ~QStyleOptionViewItem::HasDisplay;
    const QWidget* widget = option.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
}
} // namespace

// ---------------------------------------------------------------- PreviewDelegate

PreviewDelegate::PreviewDelegate(TaskListModel* model, ThumbnailService* thumbnails, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_model(model)
    , m_thumbs(thumbnails)
{
}

void PreviewDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index); // 选中背景
    if (index.row() >= m_model->rowCount())
        return;
    const StickerTask& task = m_model->taskAt(index.row());
    const qreal dpr = painter->device()->devicePixelRatioF();
    const QImage image = m_thumbs->thumbnail(ThumbnailService::requestFor(task, kThumbBox * dpr));

    const QRect cell = option.rect.adjusted(4, 4, -4, -4);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    if (image.isNull()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x22, 0x26, 0x2e));
        painter->drawRoundedRect(QRectF(cell.center().x() - 27, cell.center().y() - 20, 54, 40), 6, 6);
    } else {
        QSizeF size = QSizeF(image.size()) / dpr;
        size.scale(QSizeF(cell.size()).boundedTo(QSizeF(kThumbBox)), Qt::KeepAspectRatio);
        const QRectF target(QPointF(cell.center().x() - size.width() / 2.0, cell.center().y() - size.height() / 2.0), size);
        if (task.cropRadius <= 0) {
            QPainterPath path;
            path.addRoundedRect(target, 4, 4);
            painter->setClipPath(path);
        }
        painter->drawImage(target, image); // 圆角已在缩略图里
        painter->setClipping(false);
    }

    if (task.info.isVideo) {
        // 右下角时长角标
        QFont f = painter->font();
        f.setPointSizeF(f.pointSizeF() * 0.78);
        f.setBold(true);
        painter->setFont(f);
        const QString text = formatSecondsShort(task.effectiveDuration()) + QStringLiteral("s");
        const QFontMetrics fm(f);
        const QRectF badge(cell.right() - fm.horizontalAdvance(text) - 8, cell.bottom() - fm.height() - 1,
                           fm.horizontalAdvance(text) + 8, fm.height() + 1);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 170));
        painter->drawRoundedRect(badge, 4, 4);
        painter->setPen(Qt::white);
        painter->drawText(badge, Qt::AlignCenter, text);
    }
    painter->restore();
}

// ---------------------------------------------------------------- KeywordsDelegate

KeywordsDelegate::KeywordsDelegate(TaskListModel* model, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_model(model)
{
}

QRect KeywordsDelegate::buttonRect(const QStyleOptionViewItem& option)
{
    const int h = 26;
    return QRect(option.rect.left() + 6, option.rect.center().y() - h / 2, option.rect.width() - 12, h);
}

void KeywordsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    paintBackground(painter, option, index);
    if (index.row() >= m_model->rowCount())
        return;
    const QStringList keywords = m_model->taskAt(index.row()).keywords;
    const QRect button = buttonRect(option);
    const bool hover = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QFont f = painter->font();
    f.setPointSizeF(f.pointSizeF() * 0.92);
    painter->setFont(f);
    const QFontMetrics fm(f);

    if (keywords.isEmpty()) {
        painter->setPen(QPen(QColor(hover ? 0x5a : 0x3a, hover ? 0x62 : 0x40, hover ? 0x75 : 0x4d), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(button).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
        painter->setPen(QColor(0x8b, 0x93, 0xa7));
        painter->drawText(button, Qt::AlignCenter, tr("+ 设置关键词"));
    } else {
        painter->setPen(QPen(QColor(42, 171, 238, hover ? 200 : 110), 1));
        painter->setBrush(QColor(42, 171, 238, hover ? 50 : 30));
        painter->drawRoundedRect(QRectF(button).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
        // 右侧数量角标
        const QString count = QString::number(keywords.size());
        const int badgeW = std::max(18, fm.horizontalAdvance(count) + 10);
        const QRect badge(button.right() - badgeW - 4, button.center().y() - 9, badgeW, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x2a, 0xab, 0xee));
        painter->drawRoundedRect(badge, 9, 9);
        painter->setPen(Qt::white);
        painter->drawText(badge, Qt::AlignCenter, count);

        QString text = keywords.first();
        if (keywords.size() > 1)
            text += QStringLiteral(" +%1").arg(keywords.size() - 1);
        const QRect textRect(button.left() + 10, button.top(), badge.left() - button.left() - 14, button.height());
        painter->setPen(QColor(0xcf, 0xe9, 0xfb));
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(text, Qt::ElideRight, textRect.width()));
    }
    painter->restore();
}

bool KeywordsDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && buttonRect(option).contains(mouse->position().toPoint())) {
            emit clicked(index.row());
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// ---------------------------------------------------------------- ActionsDelegate

ActionsDelegate::ActionsDelegate(TaskListModel* model, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_model(model)
{
}

QList<int> ActionsDelegate::actionsFor(int row) const
{
    QList<int> actions{Edit};
    if (row >= 0 && row < m_model->rowCount() && m_model->taskAt(row).status == TaskStatus::Succeeded
        && !m_model->taskAt(row).outputPath.isEmpty()) {
        actions << OpenFolder;
    }
    actions << Remove;
    return actions;
}

int ActionsDelegate::actionAt(const QStyleOptionViewItem& option, int row, QPoint pos) const
{
    const QList<int> actions = actionsFor(row);
    const int total = int(actions.size()) * kActionWidth;
    const int left = option.rect.center().x() - total / 2;
    const int i = (pos.x() - left) / kActionWidth;
    if (pos.x() < left || i < 0 || i >= actions.size())
        return None;
    return actions.at(i);
}

void ActionsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    paintBackground(painter, option, index);
    const QList<int> actions = actionsFor(index.row());
    const int total = int(actions.size()) * kActionWidth;
    int x = option.rect.center().x() - total / 2;
    painter->save();
    QFont f = painter->font();
    f.setPointSizeF(f.pointSizeF() * 1.1);
    painter->setFont(f);
    for (const int a : actions) {
        const QString icon = a == Edit ? QStringLiteral("✂️") : a == OpenFolder ? QStringLiteral("📂") : QStringLiteral("🗑️");
        painter->drawText(QRect(x, option.rect.top(), kActionWidth, option.rect.height()), Qt::AlignCenter, icon);
        x += kActionWidth;
    }
    painter->restore();
}

bool ActionsDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const int action = actionAt(option, index.row(), mouse->position().toPoint());
            if (action != None) {
                emit triggered(index.row(), action);
                return true;
            }
        }
    }
    // 点击按钮时不改变选择
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (actionAt(option, index.row(), mouse->position().toPoint()) != None)
            return true;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool ActionsDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const int action = actionAt(option, index.row(), event->pos());
    QString tip;
    if (action == Edit)
        tip = m_model->taskAt(index.row()).info.isVideo ? tr("截取画面与视频片段") : tr("裁切画面样式");
    else if (action == OpenFolder)
        tip = tr("打开所在文件夹");
    else if (action == Remove)
        tip = tr("移除此项");
    if (tip.isEmpty())
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    QToolTip::showText(event->globalPos(), tip, view);
    return true;
}

} // namespace tsm
