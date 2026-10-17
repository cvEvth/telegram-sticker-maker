#include "ui/TaskTableView.h"

#include <QContextMenuEvent>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <algorithm>

namespace tsm {

namespace {
QItemSelection rowSelection(QAbstractItemModel* model, int from, int to)
{
    const int last = model->columnCount() - 1;
    return QItemSelection(model->index(std::min(from, to), 0), model->index(std::max(from, to), last));
}
} // namespace

TaskTableView::TaskTableView(QWidget* parent)
    : QTableView(parent)
{
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::NoSelection);
}

QList<int> TaskTableView::selectedRowList() const
{
    QList<int> rows;
    if (!selectionModel())
        return rows;
    for (const QModelIndex& index : selectionModel()->selectedRows())
        rows << index.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

void TaskTableView::selectRows(const QList<int>& rows)
{
    QItemSelection selection;
    for (const int row : rows)
        selection.merge(rowSelection(model(), row, row), QItemSelectionModel::Select);
    selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (!rows.isEmpty())
        m_anchor = rows.last();
}

void TaskTableView::selectAllRows()
{
    if (model()->rowCount() == 0)
        return;
    selectionModel()->select(rowSelection(model(), 0, model()->rowCount() - 1), QItemSelectionModel::ClearAndSelect);
}

void TaskTableView::invertSelection()
{
    const QList<int> selected = selectedRowList();
    QList<int> rows;
    for (int i = 0; i < model()->rowCount(); ++i) {
        if (!selected.contains(i))
            rows << i;
    }
    selectRows(rows);
}

void TaskTableView::clearRowSelection()
{
    selectionModel()->clearSelection();
    m_anchor = -1;
}

void TaskTableView::selectRange(int from, int to)
{
    selectionModel()->select(rowSelection(model(), from, to), QItemSelectionModel::ClearAndSelect);
}

void TaskTableView::mousePressEvent(QMouseEvent* event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    // 交给基类：记录按下位置（双击编辑、按钮委托都依赖它）。NoSelection 下它不会改动选择。
    QTableView::mousePressEvent(event);
    if (event->button() != Qt::LeftButton)
        return;

    if (!index.isValid()) {
        clearRowSelection(); // 点击列表空白处
        return;
    }
    if (m_buttonColumns.contains(index.column()))
        return;

    const int row = index.row();
    const Qt::KeyboardModifiers mods = event->modifiers();
    QItemSelectionModel* sel = selectionModel();
    const bool selected = sel->isRowSelected(row, {});
    if (mods & Qt::ControlModifier) {
        sel->select(rowSelection(model(), row, row), QItemSelectionModel::Toggle);
        // 锚点 = 上次选中的行；取消选中时退回到仍被选中的最后一行
        if (!selected) {
            m_anchor = row;
        } else if (m_anchor == row) {
            const QList<int> remaining = selectedRowList();
            m_anchor = remaining.isEmpty() ? -1 : remaining.last();
        }
    } else if ((mods & Qt::ShiftModifier) && m_anchor >= 0 && m_anchor < model()->rowCount()) {
        selectRange(m_anchor, row);
    } else if (selected) {
        sel->select(rowSelection(model(), row, row), QItemSelectionModel::Deselect); // 再次单击取消选中
        m_anchor = -1;
    } else {
        selectRange(row, row);
        m_anchor = row;
    }
    sel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
}

void TaskTableView::contextMenuEvent(QContextMenuEvent* event)
{
    const QModelIndex index = indexAt(event->pos());
    if (!index.isValid())
        return;
    if (!selectionModel()->isRowSelected(index.row(), {})) {
        selectRange(index.row(), index.row()); // 右键未选中的行：先只选中它
        m_anchor = index.row();
    }
    emit rowContextMenuRequested(event->globalPos());
}

} // namespace tsm
