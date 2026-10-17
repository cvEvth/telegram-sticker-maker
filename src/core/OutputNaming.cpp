#include "core/OutputNaming.h"

#include <QDir>

namespace tsm::OutputNaming {

namespace {
template <typename IsTaken>
QString firstFreeName(const QString& fileName, IsTaken isTaken)
{
    if (!isTaken(fileName))
        return fileName;
    const int dot = fileName.lastIndexOf(u'.');
    const QString base = dot > 0 ? fileName.left(dot) : fileName;
    const QString ext = dot > 0 ? fileName.mid(dot) : QString();
    for (int n = 2;; ++n) {
        const QString candidate = base + QStringLiteral("_") + QString::number(n) + ext;
        if (!isTaken(candidate))
            return candidate;
    }
}
} // namespace

QString sanitizeEmoji(const QString& emoji)
{
    static const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    QString out;
    out.reserve(emoji.size());
    for (const QChar c : emoji) {
        if (c.unicode() < 0x20 || forbidden.contains(c))
            continue;
        out += c;
    }
    out = out.trimmed();
    while (out.endsWith(u'.'))
        out.chop(1);
    return out.trimmed();
}

QString stickerFileName(int index, const QString& emoji, const QString& extension)
{
    QString name = QStringLiteral("%1").arg(index, 3, 10, QChar(u'0'));
    const QString cleanEmoji = sanitizeEmoji(emoji);
    if (!cleanEmoji.isEmpty())
        name += QStringLiteral("_") + cleanEmoji;
    return name + QStringLiteral(".") + extension;
}

QString timestampedName(const QString& prefix, const QDateTime& time)
{
    return prefix + QStringLiteral("_") + time.toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString uniqueName(const QString& fileName, const QSet<QString>& takenLower)
{
    return firstFreeName(fileName, [&](const QString& c) { return takenLower.contains(c.toLower()); });
}

QString uniqueFileName(const QString& dir, const QString& fileName)
{
    const QDir directory(dir);
    return firstFreeName(fileName, [&](const QString& c) { return directory.exists(c); });
}

} // namespace tsm::OutputNaming
