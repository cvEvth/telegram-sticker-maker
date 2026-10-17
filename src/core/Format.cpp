#include "core/Format.h"

#include <QStringList>
#include <cmath>

namespace tsm {

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatTimecode(double seconds)
{
    if (!(seconds > 0) || !std::isfinite(seconds))
        seconds = 0;
    const qint64 totalMs = qRound64(seconds * 1000.0);
    const qint64 h = totalMs / 3600000;
    const qint64 m = (totalMs / 60000) % 60;
    const qint64 s = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    const QChar zero(u'0');
    if (h > 0) {
        return QStringLiteral("%1:%2:%3.%4")
            .arg(h, 2, 10, zero).arg(m, 2, 10, zero).arg(s, 2, 10, zero).arg(ms, 3, 10, zero);
    }
    return QStringLiteral("%1:%2.%3").arg(m, 2, 10, zero).arg(s, 2, 10, zero).arg(ms, 3, 10, zero);
}

std::optional<double> parseTimecode(const QString& text)
{
    QString t = text.trimmed();
    if (t.endsWith(u's', Qt::CaseInsensitive))
        t.chop(1);
    t = t.trimmed();
    if (t.isEmpty())
        return std::nullopt;

    const QStringList parts = t.split(u':');
    if (parts.size() > 3)
        return std::nullopt;

    double total = 0;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const double v = parts[i].trimmed().toDouble(&ok);
        if (!ok || v < 0 || !std::isfinite(v))
            return std::nullopt;
        const bool last = i == parts.size() - 1;
        if (!last && v != std::floor(v))
            return std::nullopt; // 只有最后一段允许小数
        if (i > 0 && v >= 60)
            return std::nullopt; // 分、秒必须小于 60
        total = total * 60 + v;
    }
    return total;
}

QString formatSecondsShort(double seconds)
{
    QString s = QString::number(seconds, 'f', 2);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.'))
        s.chop(1);
    return s;
}

double normalizeFrameRate(double fps)
{
    if (!(fps > 0) || !std::isfinite(fps))
        return 0;
    const double rounded = std::round(fps);
    return std::abs(fps - rounded) < 0.05 ? rounded : fps;
}

QString formatFps(double fps)
{
    return formatSecondsShort(fps);
}

} // namespace tsm
