#include "core/TaskListModel.h"

#include "app/AppPaths.h"
#include "core/Format.h"
#include "core/OutputNaming.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace tsm {

namespace {
QString nameText(const StickerTask& t)
{
    QStringList tags;
    if (t.info.isVideo && t.isTrimmed())
        tags << t.clipLabel();
    if (t.mirror)
        tags << QStringLiteral("🪞 镜像");
    if (const QString crop = t.cropTag(); !crop.isEmpty())
        tags << crop;
    if (tags.isEmpty())
        return t.info.fileName;
    return t.info.fileName + QStringLiteral("\n") + tags.join(QStringLiteral("  "));
}

QString specText(const StickerTask& t)
{
    const QString size = QStringLiteral("%1×%2").arg(t.info.width).arg(t.info.height);
    if (t.info.isVideo) {
        QString text = QStringLiteral("%1 | %2s | %3fps")
                           .arg(size, formatSecondsShort(t.effectiveDuration()), formatFps(t.info.fps));
        if (t.info.hasAlpha)
            text += QStringLiteral(" 🟢Alpha");
        return text;
    }
    return size + QStringLiteral(" | 静态图") + (t.info.hasAlpha ? QStringLiteral(" 🟢透明") : QString());
}

QString keywordsText(const StickerTask& t)
{
    if (t.keywords.isEmpty())
        return QStringLiteral("+ 设置关键词");
    if (t.keywords.size() == 1)
        return t.keywords.first();
    return QStringLiteral("%1 +%2").arg(t.keywords.first()).arg(t.keywords.size() - 1);
}

QVariant statusColor(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Succeeded: return QColor(0x3e, 0xcf, 0x8e);
    case TaskStatus::Failed: return QColor(0xf0, 0x5d, 0x5e);
    case TaskStatus::Cancelled: return QColor(0x8b, 0x93, 0xa7);
    default: return {};
    }
}
} // namespace

TaskListModel::TaskListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int TaskListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_tasks.size());
}

int TaskListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TaskListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_tasks.size())
        return {};
    const int row = index.row();
    const StickerTask& t = m_tasks.at(row);

    switch (role) {
    case TaskIdRole: return t.id;
    case ProgressRole: return t.progress;
    case StatusRole: return int(t.status);
    default: break;
    }

    switch (index.column()) {
    case ColName:
        if (role == Qt::DisplayRole)
            return nameText(t);
        if (role == Qt::ToolTipRole)
            return QDir::toNativeSeparators(t.inputPath);
        break;
    case ColSpec:
        if (role == Qt::DisplayRole)
            return specText(t);
        if (role == Qt::ToolTipRole && t.info.isVideo && t.isTrimmed()) {
            return QStringLiteral("有效时长: %1s (原片: %2s)")
                .arg(t.effectiveDuration(), 0, 'f', 2)
                .arg(t.info.duration, 0, 'f', 2);
        }
        break;
    case ColTarget:
        if (role == Qt::DisplayRole)
            return targetFileName(row);
        if (role == Qt::EditRole)
            return t.emoji;
        if (role == Qt::ToolTipRole) {
            const QString path = QDir(targetDirectory(row)).filePath(targetFileName(row));
            return QDir::toNativeSeparators(path) + QStringLiteral("\n双击可修改关联 Emoji");
        }
        break;
    case ColKeywords:
        if (role == Qt::DisplayRole)
            return keywordsText(t);
        if (role == Qt::ToolTipRole && !t.keywords.isEmpty())
            return t.keywords.join(QStringLiteral("、"));
        break;
    case ColStatus:
        if (role == Qt::DisplayRole) {
            if (t.status == TaskStatus::Running)
                return QStringLiteral("%1  %2%").arg(t.statusText).arg(t.progress);
            return t.statusText;
        }
        if (role == Qt::ToolTipRole)
            return t.statusText;
        if (role == Qt::ForegroundRole)
            return statusColor(t.status);
        break;
    case ColSize:
        if (role == Qt::DisplayRole) {
            const bool converted = t.status == TaskStatus::Succeeded && t.outputSize > 0;
            return formatBytes(converted ? t.outputSize : t.info.fileSize);
        }
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case ColActions:
        // TODO(主列表): 改为可点击的按钮委托
        if (role == Qt::DisplayRole) {
            QStringList icons{QStringLiteral("✂️")};
            if (t.status == TaskStatus::Succeeded)
                icons << QStringLiteral("📂");
            icons << QStringLiteral("🗑️");
            return icons.join(QStringLiteral("  "));
        }
        if (role == Qt::ToolTipRole)
            return t.info.isVideo ? QStringLiteral("截取画面与视频片段") : QStringLiteral("裁切画面样式");
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignCenter);
        break;
    case ColPreview:
        // TODO(主列表): 缩略图（片段起点帧，应用裁切 / 圆角 / 镜像），懒加载 + 缓存
        break;
    default:
        break;
    }
    return {};
}

QVariant TaskListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const QStringList titles{
        QStringLiteral("预览"),     QStringLiteral("原文件名"), QStringLiteral("规格"),
        QStringLiteral("目标文件名"), QStringLiteral("关键词"),   QStringLiteral("进度与状态"),
        QStringLiteral("体积"),     QStringLiteral("操作"),
    };
    return section >= 0 && section < titles.size() ? QVariant(titles.at(section)) : QVariant();
}

Qt::ItemFlags TaskListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColTarget)
        f |= Qt::ItemIsEditable; // 双击行内编辑 Emoji
    return f;
}

bool TaskListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != ColTarget)
        return false;
    const QString emoji = value.toString().trimmed();
    if (m_tasks[index.row()].emoji == emoji)
        return false;
    updateTask(index.row(), [&](StickerTask& t) { t.emoji = emoji; });
    return true;
}

int TaskListModel::rowOfId(const QString& id) const
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).id == id)
            return i;
    }
    return -1;
}

void TaskListModel::appendTasks(const QList<StickerTask>& tasks)
{
    if (tasks.isEmpty())
        return;
    const int first = int(m_tasks.size());
    beginInsertRows({}, first, first + int(tasks.size()) - 1);
    m_tasks.append(tasks);
    endInsertRows();
    emit contentChanged();
}

void TaskListModel::removeTaskRows(QList<int> rows)
{
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    int lowest = -1;
    for (const int row : rows) {
        if (row < 0 || row >= m_tasks.size())
            continue;
        beginRemoveRows({}, row, row);
        m_tasks.removeAt(row);
        endRemoveRows();
        lowest = row;
    }
    if (lowest < 0)
        return;
    emitTargetColumnChanged(lowest); // 后面的行重新编号
    emit contentChanged();
}

void TaskListModel::clearTasks()
{
    if (m_tasks.isEmpty())
        return;
    beginResetModel();
    m_tasks.clear();
    endResetModel();
    emit contentChanged();
}

void TaskListModel::updateTask(int row, const std::function<void(StickerTask&)>& mutate, bool persistent)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    mutate(m_tasks[row]);
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    if (persistent)
        emit contentChanged();
}

QString TaskListModel::targetFileName(int row) const
{
    const StickerTask& t = m_tasks.at(row);
    return OutputNaming::stickerFileName(row + 1, t.emoji, t.outputExtension());
}

QString TaskListModel::targetDirectory(int row) const
{
    if (m_besideSource)
        return QFileInfo(m_tasks.at(row).inputPath).absolutePath();
    return m_outputDir.isEmpty() ? AppPaths::defaultOutputDir() : m_outputDir;
}

void TaskListModel::setOutputLocation(const QString& outputDir, bool besideSource)
{
    m_outputDir = outputDir;
    m_besideSource = besideSource;
    emitTargetColumnChanged(0);
}

void TaskListModel::emitTargetColumnChanged(int fromRow)
{
    if (fromRow < m_tasks.size())
        emit dataChanged(index(fromRow, ColTarget), index(int(m_tasks.size()) - 1, ColTarget));
}

} // namespace tsm
