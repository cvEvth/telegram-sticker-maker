#include "ui/editor/PreviewCanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

namespace tsm {

namespace {
constexpr double kMargin = 10;
constexpr double kHandleHit = 12;
constexpr double kCornerLen = 16;
constexpr double kClickSlop = 3;
} // namespace

PreviewCanvas::PreviewCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus); // 点击画面时把焦点从输入框移走，快捷键恢复可用
    setMinimumSize(320, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PreviewCanvas::setSourceSize(QSize size)
{
    m_source = size;
    update();
}

void PreviewCanvas::setImage(const QImage& image)
{
    m_image = image;
    update();
}

void PreviewCanvas::setMirror(bool mirror)
{
    m_mirror = mirror;
    update();
}

void PreviewCanvas::setCropVisible(bool visible)
{
    m_cropVisible = visible;
    updateCursor(CropMath::Handle::None);
    update();
}

void PreviewCanvas::setCrop(std::optional<QRect> crop)
{
    m_crop = crop;
    update();
}

void PreviewCanvas::setRadius(double radius)
{
    m_radius = radius;
    update();
}

void PreviewCanvas::setAspect(std::optional<double> aspect)
{
    m_aspect = aspect;
}

void PreviewCanvas::setPlaceholder(const QString& text)
{
    m_placeholder = text;
    update();
}

QRectF PreviewCanvas::displayRect() const
{
    const QRectF area = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    if (!m_source.isValid() || area.width() <= 0 || area.height() <= 0)
        return area;
    QSizeF size(m_source);
    size.scale(area.size(), Qt::KeepAspectRatio);
    return QRectF(area.center() - QPointF(size.width() / 2, size.height() / 2), size);
}

double PreviewCanvas::scale() const
{
    return m_source.width() > 0 ? displayRect().width() / m_source.width() : 1.0;
}

QRectF PreviewCanvas::toWidget(const QRectF& source) const
{
    const QRectF d = displayRect();
    const double s = scale();
    return {d.left() + source.left() * s, d.top() + source.top() * s, source.width() * s, source.height() * s};
}

double PreviewCanvas::cornerRadius(const QRectF& widgetCrop) const
{
    return std::min(widgetCrop.width(), widgetCrop.height()) / 2.0 * std::clamp(m_radius, 0.0, 1.0);
}

void PreviewCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(0x0f, 0x11, 0x15));

    const QRectF d = displayRect();
    if (m_image.isNull()) {
        p.setPen(QColor(0x8b, 0x93, 0xa7));
        p.drawText(rect(), Qt::AlignCenter, m_placeholder.isEmpty() ? QStringLiteral("加载中...") : m_placeholder);
        return;
    }

    // 透明素材显示棋盘格背景
    if (m_image.hasAlphaChannel()) {
        QPixmap checker(16, 16);
        checker.fill(QColor(0x2a, 0x2e, 0x37));
        QPainter cp(&checker);
        cp.fillRect(0, 0, 8, 8, QColor(0x22, 0x25, 0x2d));
        cp.fillRect(8, 8, 8, 8, QColor(0x22, 0x25, 0x2d));
        cp.end();
        p.fillRect(d, QBrush(checker));
    }

    p.save();
    if (m_mirror) {
        p.translate(d.center().x(), 0);
        p.scale(-1, 1);
        p.translate(-d.center().x(), 0);
    }
    p.drawImage(d, m_image);
    p.restore();

    if (!m_cropVisible || !m_crop)
        return;

    const QRectF c = toWidget(QRectF(*m_crop));
    const double r = cornerRadius(c);
    QPainterPath cropPath;
    cropPath.addRoundedRect(c, r, r);

    // 框外压暗
    QPainterPath outside;
    outside.addRect(d);
    p.fillPath(outside.subtracted(cropPath), QColor(0, 0, 0, 150));

    // 三分线
    p.save();
    p.setClipPath(cropPath);
    p.setPen(QPen(QColor(255, 255, 255, 70), 1));
    for (int i = 1; i <= 2; ++i) {
        const double x = c.left() + c.width() * i / 3;
        const double y = c.top() + c.height() * i / 3;
        p.drawLine(QPointF(x, c.top()), QPointF(x, c.bottom()));
        p.drawLine(QPointF(c.left(), y), QPointF(c.right(), y));
    }
    p.restore();

    p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(cropPath);

    // 四角 L 形把手
    const QColor accent(0x2a, 0xab, 0xee);
    p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
    const double len = std::min({kCornerLen, c.width() / 3, c.height() / 3});
    const QPointF corners[] = {c.topLeft(), c.topRight(), c.bottomRight(), c.bottomLeft()};
    const QPointF dirs[] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    for (int i = 0; i < 4; ++i) {
        const QPointF pt = corners[i];
        p.drawLine(pt, pt + QPointF(dirs[i].x() * len, 0));
        p.drawLine(pt, pt + QPointF(0, dirs[i].y() * len));
    }
    // 四边中点胶囊把手
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(c.center().x() - 10, c.top() - 3, 20, 6), 3, 3);
    p.drawRoundedRect(QRectF(c.center().x() - 10, c.bottom() - 3, 20, 6), 3, 3);
    p.drawRoundedRect(QRectF(c.left() - 3, c.center().y() - 10, 6, 20), 3, 3);
    p.drawRoundedRect(QRectF(c.right() - 3, c.center().y() - 10, 6, 20), 3, 3);
}

CropMath::Handle PreviewCanvas::hitTest(QPointF pos) const
{
    using H = CropMath::Handle;
    if (!m_cropVisible || !m_crop)
        return H::None;
    const QRectF c = toWidget(QRectF(*m_crop));
    const auto near = [&](QPointF a) { return QLineF(a, pos).length() <= kHandleHit; };
    if (near(c.topLeft())) return H::TopLeft;
    if (near(c.topRight())) return H::TopRight;
    if (near(c.bottomRight())) return H::BottomRight;
    if (near(c.bottomLeft())) return H::BottomLeft;
    const bool withinX = pos.x() >= c.left() && pos.x() <= c.right();
    const bool withinY = pos.y() >= c.top() && pos.y() <= c.bottom();
    if (withinX && std::abs(pos.y() - c.top()) <= kHandleHit / 2) return H::Top;
    if (withinX && std::abs(pos.y() - c.bottom()) <= kHandleHit / 2) return H::Bottom;
    if (withinY && std::abs(pos.x() - c.left()) <= kHandleHit / 2) return H::Left;
    if (withinY && std::abs(pos.x() - c.right()) <= kHandleHit / 2) return H::Right;
    if (c.contains(pos)) return H::Move;
    return H::None;
}

void PreviewCanvas::updateCursor(CropMath::Handle handle)
{
    using H = CropMath::Handle;
    switch (handle) {
    case H::TopLeft:
    case H::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case H::TopRight:
    case H::BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
    case H::Left:
    case H::Right: setCursor(Qt::SizeHorCursor); break;
    case H::Top:
    case H::Bottom: setCursor(Qt::SizeVerCursor); break;
    case H::Move: setCursor(Qt::SizeAllCursor); break;
    case H::None: setCursor(m_image.isNull() ? Qt::ArrowCursor : Qt::PointingHandCursor); break;
    }
}

void PreviewCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_moved = false;
    m_pressPos = e->position();
    m_dragHandle = hitTest(e->position());
    if (m_crop)
        m_dragStart = QRectF(*m_crop);
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_pressed) {
        updateCursor(hitTest(e->position()));
        return;
    }
    const QPointF deltaWidget = e->position() - m_pressPos;
    if (!m_moved && deltaWidget.manhattanLength() < kClickSlop)
        return;
    m_moved = true;
    if (m_dragHandle == CropMath::Handle::None || !m_crop)
        return;
    const double s = scale();
    const QPointF delta(deltaWidget.x() / s, deltaWidget.y() / s);
    const QRectF next = CropMath::resized(m_dragStart, m_dragHandle, delta, m_aspect, m_source);
    const QRect aligned = CropMath::evenAligned(next, m_source);
    if (aligned != *m_crop) {
        m_crop = aligned;
        update();
        emit cropEdited(aligned);
    }
}

void PreviewCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_pressed)
        return;
    m_pressed = false;
    // 没拖动 = 单击画面：播放 / 暂停
    if (!m_moved)
        emit clicked();
    m_dragHandle = CropMath::Handle::None;
    updateCursor(hitTest(e->position()));
}

void PreviewCanvas::wheelEvent(QWheelEvent* e)
{
    emit wheelZoom(e->angleDelta().y() / 120.0);
    e->accept();
}

} // namespace tsm
