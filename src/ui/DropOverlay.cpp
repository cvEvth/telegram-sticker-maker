#include "ui/DropOverlay.h"

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
    painter.fillRect(rect(), QColor(10, 12, 16, 200));

    const QRectF box = QRectF(rect()).adjusted(28, 28, -28, -28);
    QPen pen(QColor(0x2a, 0xab, 0xee), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(QColor(42, 171, 238, 20));
    painter.drawRoundedRect(box, 18, 18);

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 2.2);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(0xe6, 0xe8, 0xee));
    painter.drawText(box, Qt::AlignCenter, tr("📥\n松开鼠标立即导入文件"));
}

} // namespace tsm
