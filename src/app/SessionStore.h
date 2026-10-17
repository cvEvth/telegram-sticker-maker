#pragma once

#include <QByteArray>
#include <QObject>

class QTimer;

namespace tsm {

class TaskListModel;

// 会话自动保存（需求第 10 节）：列表任何变化 250ms 防抖后原子写入数据目录的会话文件；
// 内容没变就不重复写；恢复期间暂停保存。
class SessionStore : public QObject {
    Q_OBJECT

public:
    static constexpr int kDebounceMs = 250;

    SessionStore(TaskListModel* model, QObject* parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString& path);
    // 切换数据目录：新目录还没有会话文件时，把旧目录的会话复制过去
    void moveTo(const QString& newPath);

    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }

    QByteArray serialize() const;
    // 立即保存（关闭窗口前调用）；返回是否成功或无需写入
    bool saveNow();

signals:
    void saveFailed(const QString& error);

private:
    TaskListModel* m_model;
    QTimer* m_timer;
    QString m_path;
    QByteArray m_lastWritten;
    bool m_suspended = false;
};

} // namespace tsm
