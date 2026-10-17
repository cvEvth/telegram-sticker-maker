#include "media/WebpEncoder.h"

#include <webp/encode.h>

namespace tsm::WebpEncoder {

namespace {
constexpr int kNearLosslessLevel = 60; // 0 = 最强预处理，100 = 关闭
}

QByteArray encode(const QImage& image, Mode mode, int quality, QString* error)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) {
        if (error)
            *error = QStringLiteral("图像为空");
        return {};
    }

    WebPConfig config;
    if (!WebPConfigInit(&config)) {
        if (error)
            *error = QStringLiteral("libwebp 版本不匹配");
        return {};
    }
    config.method = 6;
    switch (mode) {
    case Mode::Lossless:
        config.lossless = 1;
        config.quality = 100; // 无损模式下表示压缩力度
        break;
    case Mode::NearLossless:
        config.lossless = 1;
        config.quality = 100;
        config.near_lossless = kNearLosslessLevel;
        break;
    case Mode::Lossy:
        config.lossless = 0;
        config.quality = float(quality);
        config.alpha_quality = 100;
        config.use_sharp_yuv = 1;
        break;
    }

    WebPPicture picture;
    if (!WebPPictureInit(&picture)) {
        if (error)
            *error = QStringLiteral("libwebp 初始化失败");
        return {};
    }
    picture.use_argb = 1;
    picture.width = rgba.width();
    picture.height = rgba.height();

    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    QByteArray out;
    if (!WebPPictureImportRGBA(&picture, rgba.constBits(), int(rgba.bytesPerLine()))) {
        if (error)
            *error = QStringLiteral("WebP 导入像素失败");
    } else if (!WebPEncode(&config, &picture)) {
        if (error)
            *error = QStringLiteral("WebP 编码失败 (错误码 %1)").arg(int(picture.error_code));
    } else {
        out = QByteArray(reinterpret_cast<const char*>(writer.mem), qsizetype(writer.size));
    }
    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);
    return out;
}

} // namespace tsm::WebpEncoder
