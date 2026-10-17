#pragma once

#include <QTableView>

namespace tsm {

// 主任务列表的选择规则（需求 3.3 / 3.4）：
//  - 单击选中该行；再次单击已选中的行取消选中
//  - Ctrl/⌘ + 单击切换；Shift + 单击从上次选中行做范围选择
//  - 点击空白处取消选择；右键未选中的行时先只选中它
// 选择完全由这里管理（视图的 selectionMode 为 NoSelection），按钮类列（关键词 / 操作）点击不改变选择。
class TaskTableView : public QTableView {
    Q_OBJECT

public:
    explicit TaskTableView(QWidget* parent = nullptr);

    void setButtonColumns(const QList<int>& columns) { m_buttonColumns = columns; }

    QList<int> selectedRowList() const;
    void selectRows(const QList<int>& rows);
    void selectAllRows();
    void invertSelection();
    void clearRowSelection();

signals:
    // 选择已按右键规则调整好
    void rowContextMenuRequested(const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void selectRange(int from, int to);

    QList<int> m_buttonColumns;
    int m_anchor = -1;
};

} // namespace tsm
