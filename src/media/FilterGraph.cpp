#include "media/FilterGraph.h"

#include "core/CropMath.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <algorithm>

namespace tsm::FilterGraph {

namespace {
QString fpsValue(double fps)
{
    QString s = QString::number(fps, 'f', 3);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.'))
        s.chop(1);
    return s;
}
} // namespace

QRect evenCrop(const QRect& crop, QSize frame)
{
    return CropMath::evenAligned(QRectF(crop), frame);
}

QString presetFilter(EncodePreset preset)
{
    switch (preset) {
    // hqdn3d 是 GPL 滤镜（LGPL 版 FFmpeg 没有），atadenoise 可保留透明通道
    case EncodePreset::Quality: return QStringLiteral("atadenoise=0a=0.02:0b=0.04:1a=0.02:1b=0.04:2a=0.02:2b=0.04:s=5");
    case EncodePreset::Fast: return {};
    }
    return {};
}

bool writeRoundMask(QSize size, double radius, const QString& path)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(Qt::black);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing);
    const double r = std::min(size.width(), size.height()) / 2.0 * std::clamp(radius, 0.0, 1.0);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), r, r);
    painter.fillPath(shape, Qt::white);
    painter.end();
    return mask.save(path, "PNG");
}

QString build(const Spec& spec)
{
    QStringList chain;
    if (spec.mirror)
        chain << QStringLiteral("hflip");
    if (spec.crop) {
        chain << QStringLiteral("crop=%1:%2:%3:%4").arg(spec.crop->width()).arg(spec.crop->height()).arg(spec.crop->x()).arg(spec.crop->y());
    }
    chain << QStringLiteral("scale=%1:%2:flags=lanczos+accurate_rnd+full_chroma_int+full_chroma_inp")
                 .arg(spec.scale.width())
                 .arg(spec.scale.height());
    if (const QString preset = presetFilter(spec.preset); !preset.isEmpty())
        chain << preset;
    if (spec.fps > 0)
        chain << QStringLiteral("fps=") + fpsValue(spec.fps);

    const bool alpha = spec.rgbaOutput || spec.alphaOutput;
    if (spec.rgbaOutput) {
        chain << QStringLiteral("format=rgba");
    } else {
        // 色彩统一 BT.709、TV range
        chain << QStringLiteral("scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int")
              << (alpha ? QStringLiteral("format=yuva420p") : QStringLiteral("format=yuv420p"));
    }

    QStringList graph;
    QString current = QStringLiteral("[base]");
    graph << QStringLiteral("[0:v]") + chain.join(u',') + current;

    if (spec.roundMask) {
        graph << QStringLiteral("[1:v]format=gray,scale=%1:%2:flags=bicubic[mask]").arg(spec.scale.width()).arg(spec.scale.height());
        if (spec.sourceAlpha) {
            // 素材自带透明：把原透明通道与圆角遮罩相乘，避免覆盖原有透明区域
            graph << current + QStringLiteral("split[rgb][src]")
                  << QStringLiteral("[src]alphaextract[srca]")
                  << QStringLiteral("[srca][mask]blend=all_mode=multiply[alpha]")
                  << QStringLiteral("[rgb][alpha]alphamerge[shaped]");
        } else {
            graph << current + QStringLiteral("[mask]alphamerge[shaped]");
        }
        current = QStringLiteral("[shaped]");
    }

    if (spec.canvas.isValid() && spec.canvas != spec.scale) {
        graph << current + QStringLiteral("pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black@0[padded]").arg(spec.canvas.width()).arg(spec.canvas.height());
        current = QStringLiteral("[padded]");
    }
    graph << current + QStringLiteral("null[out]");
    return graph.join(u';');
}

} // namespace tsm::FilterGraph
