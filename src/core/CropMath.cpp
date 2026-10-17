#include "core/CropMath.h"

#include <algorithm>
#include <cmath>

namespace tsm::CropMath {

namespace {
int floorEven(double v)
{
    const int i = int(std::floor(v));
    return i - (i & 1);
}

bool isLeft(Handle h) { return h == Handle::TopLeft || h == Handle::Left || h == Handle::BottomLeft; }
bool isRight(Handle h) { return h == Handle::TopRight || h == Handle::Right || h == Handle::BottomRight; }
bool isTop(Handle h) { return h == Handle::TopLeft || h == Handle::Top || h == Handle::TopRight; }
bool isBottom(Handle h) { return h == Handle::BottomLeft || h == Handle::Bottom || h == Handle::BottomRight; }

QRectF resizedFree(const QRectF& s, Handle h, QPointF d, QSize frame)
{
    const double minSide = kMinCropSize;
    double left = s.left(), right = s.right(), top = s.top(), bottom = s.bottom();
    if (isLeft(h))
        left = std::clamp(s.left() + d.x(), 0.0, right - minSide);
    if (isRight(h))
        right = std::clamp(s.right() + d.x(), left + minSide, double(frame.width()));
    if (isTop(h))
        top = std::clamp(s.top() + d.y(), 0.0, bottom - minSide);
    if (isBottom(h))
        bottom = std::clamp(s.bottom() + d.y(), top + minSide, double(frame.height()));
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QRectF resizedCorner(const QRectF& s, Handle h, QPointF d, double a, QSize frame)
{
    // 对角固定，宽高取两个方向中变化更大的一个，保持比例
    const QPointF anchor(isLeft(h) ? s.right() : s.left(), isTop(h) ? s.bottom() : s.top());
    const QPointF corner(isLeft(h) ? s.left() : s.right(), isTop(h) ? s.top() : s.bottom());
    const QPointF mouse = corner + d;
    const double rawW = std::abs(mouse.x() - anchor.x());
    const double rawH = std::abs(mouse.y() - anchor.y());
    double w = std::max(rawW, rawH * a);

    const double maxW = isLeft(h) ? anchor.x() : frame.width() - anchor.x();
    const double maxH = isTop(h) ? anchor.y() : frame.height() - anchor.y();
    const double minW = std::max(double(kMinCropSize), kMinCropSize * a);
    w = std::clamp(w, std::min(minW, std::min(maxW, maxH * a)), std::min(maxW, maxH * a));
    const double hgt = w / a;
    const double x = isLeft(h) ? anchor.x() - w : anchor.x();
    const double y = isTop(h) ? anchor.y() - hgt : anchor.y();
    return {x, y, w, hgt};
}

QRectF resizedEdge(const QRectF& s, Handle h, QPointF d, double a, QSize frame)
{
    const double minW = std::max(double(kMinCropSize), kMinCropSize * a);
    if (h == Handle::Left || h == Handle::Right) {
        const double anchorX = h == Handle::Left ? s.right() : s.left();
        const double maxW = std::min(h == Handle::Left ? anchorX : frame.width() - anchorX, frame.height() * a);
        const double w = std::clamp(h == Handle::Left ? s.width() - d.x() : s.width() + d.x(), std::min(minW, maxW), maxW);
        const double hgt = w / a;
        const double y = std::clamp(s.center().y() - hgt / 2, 0.0, frame.height() - hgt);
        return {h == Handle::Left ? anchorX - w : anchorX, y, w, hgt};
    }
    const double anchorY = h == Handle::Top ? s.bottom() : s.top();
    const double maxH = std::min(h == Handle::Top ? anchorY : frame.height() - anchorY, frame.width() / a);
    const double minH = minW / a;
    const double hgt = std::clamp(h == Handle::Top ? s.height() - d.y() : s.height() + d.y(), std::min(minH, maxH), maxH);
    const double w = hgt * a;
    const double x = std::clamp(s.center().x() - w / 2, 0.0, frame.width() - w);
    return {x, h == Handle::Top ? anchorY - hgt : anchorY, w, hgt};
}
} // namespace

QString aspectLabel(CropAspect aspect)
{
    switch (aspect) {
    case CropAspect::Square: return QStringLiteral("1:1");
    case CropAspect::Free: return QStringLiteral("自由比例");
    case CropAspect::Original: return QStringLiteral("原始比例");
    case CropAspect::Landscape16x9: return QStringLiteral("16:9（横屏）");
    case CropAspect::Classic4x3: return QStringLiteral("4:3（经典）");
    case CropAspect::Portrait9x16: return QStringLiteral("9:16（竖屏）");
    }
    return {};
}

std::optional<double> aspectRatio(CropAspect aspect, QSize frame)
{
    switch (aspect) {
    case CropAspect::Square: return 1.0;
    case CropAspect::Free: return std::nullopt;
    case CropAspect::Original:
        return frame.height() > 0 ? std::optional<double>(double(frame.width()) / frame.height()) : std::nullopt;
    case CropAspect::Landscape16x9: return 16.0 / 9.0;
    case CropAspect::Classic4x3: return 4.0 / 3.0;
    case CropAspect::Portrait9x16: return 9.0 / 16.0;
    }
    return std::nullopt;
}

QRect evenAligned(const QRectF& rect, QSize frame)
{
    const int maxW = floorEven(frame.width());
    const int maxH = floorEven(frame.height());
    const int w = std::clamp(floorEven(rect.width()), std::min(kMinCropSize, maxW), maxW);
    const int h = std::clamp(floorEven(rect.height()), std::min(kMinCropSize, maxH), maxH);
    const int x = std::clamp(floorEven(rect.x()), 0, floorEven(frame.width() - w));
    const int y = std::clamp(floorEven(rect.y()), 0, floorEven(frame.height() - h));
    return {x, y, w, h};
}

QRect centeredMax(QSize frame, std::optional<double> aspect)
{
    if (!aspect)
        return evenAligned(QRectF(QPointF(0, 0), QSizeF(frame)), frame);
    double w = frame.width();
    double h = w / *aspect;
    if (h > frame.height()) {
        h = frame.height();
        w = h * *aspect;
    }
    return evenAligned(QRectF((frame.width() - w) / 2, (frame.height() - h) / 2, w, h), frame);
}

QRect mirrored(const QRect& rect, QSize frame)
{
    return evenAligned(QRectF(frame.width() - rect.x() - rect.width(), rect.y(), rect.width(), rect.height()), frame);
}

QRect withSize(const QRect& rect, QSize size, QSize frame)
{
    return evenAligned(QRectF(rect.topLeft(), QSizeF(size)), frame);
}

QRectF moved(const QRectF& start, QPointF delta, QSize frame)
{
    const double x = std::clamp(start.x() + delta.x(), 0.0, std::max(0.0, frame.width() - start.width()));
    const double y = std::clamp(start.y() + delta.y(), 0.0, std::max(0.0, frame.height() - start.height()));
    return {x, y, start.width(), start.height()};
}

QRectF resized(const QRectF& start, Handle handle, QPointF delta, std::optional<double> aspect, QSize frame)
{
    if (handle == Handle::None)
        return start;
    if (handle == Handle::Move)
        return moved(start, delta, frame);
    if (!aspect || *aspect <= 0)
        return resizedFree(start, handle, delta, frame);
    const bool corner = handle == Handle::TopLeft || handle == Handle::TopRight || handle == Handle::BottomLeft
        || handle == Handle::BottomRight;
    return corner ? resizedCorner(start, handle, delta, *aspect, frame) : resizedEdge(start, handle, delta, *aspect, frame);
}

} // namespace tsm::CropMath
