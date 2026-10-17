#pragma once

#include <QStyledItemDelegate>

namespace tsm {

class TaskListModel;
class ThumbnailService;

// 预览列：缩略图（片段起点帧，应用裁切 / 圆角 / 镜像）+ 视频时长角标。只为可见行请求，即懒加载。
class PreviewDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    PreviewDelegate(TaskListModel* model, ThumbnailService* thumbnails, QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    TaskListModel* m_model;
    ThumbnailService* m_thumbs;
};

// 操作列：✂️ 裁切/截取、📂 打开所在文件夹（仅成功后）、🗑️ 移除
class ActionsDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum Action { None = -1, Edit = 0, OpenFolder, Remove };

    ActionsDelegate(TaskListModel* model, QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

signals:
    void triggered(int row, int action);

private:
    QList<int> actionsFor(int row) const;
    int actionAt(const QStyleOptionViewItem& option, int row, QPoint pos) const;

    TaskListModel* m_model;
};

} // namespace tsm
