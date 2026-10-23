#pragma once

#include "core/StickerTask.h"

#include <QAbstractTableModel>
#include <QColor>
#include <functional>

namespace tsm {

// 主任务列表。序号 = 行号 + 1，增删后目标文件名自动重新编号。
class TaskListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColPreview, ColName, ColSpec, ColTarget, ColKeywords, ColStatus, ColSize, ColActions, ColumnCount };
    enum Role { TaskIdRole = Qt::UserRole + 1, ProgressRole, StatusRole };

    explicit TaskListModel(QObject* parent = nullptr);

    // 状态列文字颜色（随界面主题变化）
    static void setStatusColors(const QColor& succeeded, const QColor& failed, const QColor& cancelled);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    const QList<StickerTask>& tasks() const { return m_tasks; }
    const StickerTask& taskAt(int row) const { return m_tasks.at(row); }
    int rowOfId(const QString& id) const;

    void appendTasks(const QList<StickerTask>& tasks);
    // 用 tasks 替换 rows（片段组写回）：在原来最靠前的位置连续插入，返回插入起点
    int replaceRows(QList<int> rows, const QList<StickerTask>& tasks);
    // 同一片段组的所有行（按列表顺序）；clipGroupId 为空时只有该行
    QList<int> clipGroupRows(int row) const;
    void removeTaskRows(QList<int> rows);
    void clearTasks();
    // persistent = false 用于进度等运行时状态，不触发会话保存
    void updateTask(int row, const std::function<void(StickerTask&)>& mutate, bool persistent = true);

    QString targetFileName(int row) const;
    QString targetDirectory(int row) const;
    void setOutputLocation(const QString& outputDir, bool besideSource);

signals:
    // 任何需要写入会话的修改（自动保存用）
    void contentChanged();

private:
    void emitTargetColumnChanged(int fromRow);

    QList<StickerTask> m_tasks;
    QString m_outputDir;
    bool m_besideSource = false;
};

} // namespace tsm
