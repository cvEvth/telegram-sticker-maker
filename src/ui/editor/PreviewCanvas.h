#pragma once

#include "core/CropMath.h"

#include <QImage>
#include <QWidget>
#include <optional>

namespace tsm {

// 截取窗口的预览区（需求 6.3 / 6.6）：显示视频帧或图片、实时镜像，叠加带圆角与三分线的裁切框。
// 裁切框坐标以原始像素保存，不受窗口缩放影响。
class PreviewCanvas : public QWidget {
    Q_OBJECT

public:
    explicit PreviewCanvas(QWidget* parent = nullptr);

    void setSourceSize(QSize size);
    void setImage(const QImage& image);
    void setMirror(bool mirror);
    void setCropVisible(bool visible);
    void setCrop(std::optional<QRect> crop);
    void setRadius(double radius);
    void setAspect(std::optional<double> aspect);
    void setPlaceholder(const QString& text);

signals:
    void cropEdited(const QRect& crop);
    void clicked();
    void wheelZoom(double steps);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRectF displayRect() const;
    double scale() const;
    QRectF toWidget(const QRectF& source) const;
    CropMath::Handle hitTest(QPointF pos) const;
    void updateCursor(CropMath::Handle handle);
    double cornerRadius(const QRectF& widgetCrop) const;

    QSize m_source;
    QImage m_image;
    QString m_placeholder;
    bool m_mirror = false;
    bool m_cropVisible = false;
    std::optional<QRect> m_crop;
    double m_radius = 0;
    std::optional<double> m_aspect;

    CropMath::Handle m_dragHandle = CropMath::Handle::None;
    QRectF m_dragStart;
    QPointF m_pressPos;
    bool m_pressed = false;
    bool m_moved = false;
};

} // namespace tsm
