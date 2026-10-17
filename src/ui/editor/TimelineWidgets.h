#pragma once

#include <QWidget>

namespace tsm {

class TimelineState;

// 片段色块的配色（时间轴、总览条、片段卡片共用）
QColor clipColor(int index);

// 主时间轴（需求 6.4）：自适应刻度、片段色块、已播放高亮、悬停竖线、播放头。
// 左键拖动 = 拖动播放头；中键 / Alt+左键 = 平移；滚轮 = 缩放；Shift+滚轮 = 平移；
// 双击片段 = 放大到该片段，双击空白 = 恢复全长。
class TimelineTrack : public QWidget {
    Q_OBJECT

public:
    explicit TimelineTrack(TimelineState* state, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {600, 78}; }

signals:
    void scrubbed(double seconds);
    void clipPressed(int index);
    void scrubFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF trackRect() const;
    double timeAt(double x) const;
    double xAt(double seconds) const;

    TimelineState* m_state;
    enum class Drag { None, Scrub, Pan } m_drag = Drag::None;
    double m_lastX = 0;
};

// 总览条：整个视频的所有片段、当前可视窗口框、播放头。拖动窗口框 = 平移；点击窗口外 = 把窗口中心移过去。
class TimelineOverview : public QWidget {
    Q_OBJECT

public:
    explicit TimelineOverview(TimelineState* state, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {600, 20}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    double timeAt(double x) const;
    double xAt(double seconds) const;

    TimelineState* m_state;
    bool m_dragging = false;
    double m_lastX = 0;
};

} // namespace tsm
