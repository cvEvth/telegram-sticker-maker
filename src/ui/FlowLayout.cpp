#include "ui/FlowLayout.h"

#include <QWidget>

namespace tsm {

FlowLayout::FlowLayout(QWidget* parent, int spacing)
    : QLayout(parent)
    , m_gap(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items << item;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for (QLayoutItem* item : m_items) {
        if (item->widget() && item->widget()->isHidden())
            continue;
        const QSize hint = item->sizeHint();
        if (x + hint.width() > area.right() + 1 && lineHeight > 0) {
            x = area.x();
            y += lineHeight + m_gap;
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + m_gap;
        lineHeight = std::max(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

} // namespace tsm
