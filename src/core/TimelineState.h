#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <optional>

namespace tsm {

// 时间轴视图状态（需求 6.4）：总览条与主时间轴共享。
class TimelineState : public QObject {
    Q_OBJECT

public:
    static constexpr double kMinSpan = 0.12;

    struct Span {
        double start = 0;
        std::optional<double> end;
        QString emoji;
    };

    explicit TimelineState(QObject* parent = nullptr);

    double duration() const { return m_duration; }
    double playhead() const { return m_playhead; }
    double viewStart() const { return m_viewStart; }
    double viewEnd() const { return m_viewEnd; }
    double viewSpan() const { return m_viewEnd - m_viewStart; }
    const QList<Span>& spans() const { return m_spans; }
    int selected() const { return m_selected; }
    std::optional<double> hover() const { return m_hover; }
    bool isZoomed() const { return viewSpan() < m_duration - 1e-6; }

    void setDuration(double seconds);
    void setPlayhead(double seconds);
    void setSpans(const QList<Span>& spans);
    void setSelected(int index);
    void setHover(std::optional<double> seconds);

    void setView(double start, double end);
    void zoomAround(double anchor, double factor); // factor < 1 放大
    void panBy(double seconds);
    void centerOn(double seconds);
    void fitRange(double start, double end);       // 留 5% 边距
    void showAll();

    // 用户手动平移后短暂暂停自动跟随
    void notifyManualPan();
    // 播放时视图跟随播放头
    void followPlayhead();

    int spanAt(double seconds) const;

    // 刻度档位：让可视范围内大约不超过 8 个刻度标签
    static double tickStep(double span);

signals:
    void changed();

private:
    double m_duration = 0;
    double m_playhead = 0;
    double m_viewStart = 0;
    double m_viewEnd = 0;
    QList<Span> m_spans;
    int m_selected = -1;
    std::optional<double> m_hover;
    QElapsedTimer m_manualPan;
};

} // namespace tsm
