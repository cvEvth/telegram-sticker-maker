#include "app/SessionStore.h"

#include "core/StickerList.h"
#include "core/TaskListModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>

namespace tsm {

SessionStore::SessionStore(TaskListModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setInterval(kDebounceMs);
    connect(m_timer, &QTimer::timeout, this, &SessionStore::saveNow);
    connect(m_model, &TaskListModel::contentChanged, this, [this] {
        if (!m_suspended)
            m_timer->start();
    });
}

void SessionStore::setPath(const QString& path)
{
    m_path = path;
    QFile existing(path);
    m_lastWritten = existing.open(QIODevice::ReadOnly) ? existing.readAll() : QByteArray();
}

void SessionStore::moveTo(const QString& newPath)
{
    if (QDir::cleanPath(newPath) == QDir::cleanPath(m_path))
        return;
    if (!QFileInfo::exists(newPath) && QFileInfo::exists(m_path)) {
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        QFile::copy(m_path, newPath);
    }
    setPath(newPath);
}

void SessionStore::setSuspended(bool suspended)
{
    m_suspended = suspended;
    if (suspended)
        m_timer->stop();
}

QByteArray SessionStore::serialize() const
{
    StickerList::WriteOptions options;
    options.exportMode = QStringLiteral("session");
    options.includeMedia = true; // 会话不保存转换状态与进度，只缓存解析结果
    return StickerList::write(m_model->tasks(), options);
}

bool SessionStore::saveNow()
{
    m_timer->stop();
    if (m_suspended || m_path.isEmpty())
        return true;
    const QByteArray data = serialize();
    if (data == m_lastWritten)
        return true;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // 先写临时文件再替换，写到一半崩溃也不会损坏原会话
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        emit saveFailed(file.errorString());
        return false;
    }
    m_lastWritten = data;
    return true;
}

} // namespace tsm
