#include "ui/TaskTableDelegates.h"

#include "core/Format.h"
#include "core/TaskListModel.h"
#include "media/ThumbnailService.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace tsm {

namespace {
constexpr QSize kThumbBox(72, 54);
constexpr int kActionWidth = 28;
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
    QStyleOptionViewItem opt = option;
    opt.text.clear();
    QStyledItemDelegate::paint(painter, opt, index);
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
        tip = m_model->taskAt(index.row()).info.isVideo ? QStringLiteral("截取画面与视频片段") : QStringLiteral("裁切画面样式");
    else if (action == OpenFolder)
        tip = QStringLiteral("打开所在文件夹");
    else if (action == Remove)
        tip = QStringLiteral("移除此项");
    if (tip.isEmpty())
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    QToolTip::showText(event->globalPos(), tip, view);
    return true;
}

} // namespace tsm
