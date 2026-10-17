#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace tsm::WebpEncoder {

enum class Mode { Lossless, NearLossless, Lossy };

// 用 libwebp 把 RGBA 图像编码为 WebP（保留透明）。quality 仅用于有损模式。
QByteArray encode(const QImage& image, Mode mode, int quality, QString* error = nullptr);

} // namespace tsm::WebpEncoder
