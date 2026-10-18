#pragma once

#include "core/Spec.h"

#include <QRect>
#include <QSize>
#include <QString>
#include <optional>

namespace tsm::FilterGraph {

// 处理顺序：镜像 → 裁切 → Lanczos 缩放 → 预设滤镜 → 帧率 → 圆角透明遮罩，
// 自定义表情再透明填充到画布中央。输入 [0:v] 为素材，[1:v] 为圆角遮罩（灰度 PNG）；输出标签 [out]。
struct Spec {
    bool mirror = false;
    std::optional<QRect> crop; // 已偶数对齐（镜像后的画面坐标）
    QSize scale;               // 缩放后的画面尺寸
    EncodePreset preset = EncodePreset::Quality;
    double fps = 0;            // 0 = 不调整帧率（静态图）
    bool roundMask = false;    // 是否有 [1:v] 圆角遮罩输入
    bool sourceAlpha = false;  // 素材本身带透明
    bool alphaOutput = false;  // 视频输出保留透明通道
    QSize canvas;              // 与 scale 不同时四周透明填充
    bool rgbaOutput = false;   // 静态图输出 RGBA，否则输出 BT.709 TV range 的 YUV 4:2:0
};

QString build(const Spec& spec);

// 裁切框 x、y、宽、高向下取偶数，并收进画面内（最小 16px）
QRect evenCrop(const QRect& crop, QSize frame);

// 预设滤镜：高画质轻度降噪 / 快速不加滤镜
QString presetFilter(EncodePreset preset);

// 圆角遮罩 PNG：圆角半径 = 短边一半 × radius；白色 = 保留，黑色 = 透明
bool writeRoundMask(QSize size, double radius, const QString& path);

} // namespace tsm::FilterGraph
