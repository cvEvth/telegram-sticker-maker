#pragma once

#include "core/Spec.h"

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <optional>

// 裁切框计算（需求 6.6）。坐标为原始视频像素（镜像后的画面坐标）。
namespace tsm::CropMath {

inline constexpr int kMinCropSize = 16;

enum class Handle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

QString aspectLabel(CropAspect aspect);
// 宽 / 高；自由比例返回空
std::optional<double> aspectRatio(CropAspect aspect, QSize frame);

// x、y、宽、高向下取偶数，最小 16，并收进画面
QRect evenAligned(const QRectF& rect, QSize frame);
// 按比例居中放到最大
QRect centeredMax(QSize frame, std::optional<double> aspect);
// 镜像开关时把框水平镜像，框住的仍是同一块内容
QRect mirrored(const QRect& rect, QSize frame);
// 保留左上角位置换成新尺寸，超出画面时收回
QRect withSize(const QRect& rect, QSize size, QSize frame);
// 平移，限制在画面内
QRectF moved(const QRectF& start, QPointF delta, QSize frame);
// 从拖动起点 start 按把手和位移调整大小（比例锁定时保持比例）
QRectF resized(const QRectF& start, Handle handle, QPointF delta, std::optional<double> aspect, QSize frame);

} // namespace tsm::CropMath
