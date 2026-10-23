#include "ui/DropOverlay.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>

namespace tsm {

DropOverlay::DropOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void DropOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& c = theme();
    QColor scrim = c.bg;
    scrim.setAlpha(215);
    painter.fillRect(rect(), scrim);

    const QRectF box = QRectF(rect()).adjusted(28, 28, -28, -28);
    QPen pen(c.accent, 2.5, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(c.accentBg);
    painter.drawRoundedRect(box, 18, 18);

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 2.2);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(c.ink);
    painter.drawText(box, Qt::AlignCenter, tr("📥\n松开鼠标立即导入文件"));
}

} // namespace tsm
