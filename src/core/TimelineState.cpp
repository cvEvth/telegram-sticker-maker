#include "core/TimelineState.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tsm {

namespace {
constexpr int kFollowPauseMs = 1500;
constexpr std::array<double, 16> kTickSteps{0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
} // namespace

TimelineState::TimelineState(QObject* parent)
    : QObject(parent)
{
}

void TimelineState::setDuration(double seconds)
{
    m_duration = std::max(0.0, seconds);
    m_viewStart = 0;
    m_viewEnd = m_duration;
    m_playhead = std::clamp(m_playhead, 0.0, m_duration);
    emit changed();
}

void TimelineState::setPlayhead(double seconds)
{
    const double t = std::clamp(seconds, 0.0, m_duration);
    if (std::abs(t - m_playhead) < 1e-6)
        return;
    m_playhead = t;
    emit changed();
}

void TimelineState::setSpans(const QList<Span>& spans)
{
    m_spans = spans;
    if (m_selected >= m_spans.size())
        m_selected = -1;
    emit changed();
}

void TimelineState::setSelected(int index)
{
    m_selected = index >= 0 && index < m_spans.size() ? index : -1;
    emit changed();
}

void TimelineState::setHover(std::optional<double> seconds)
{
    if (m_hover == seconds)
        return;
    m_hover = seconds;
    emit changed();
}

void TimelineState::setView(double start, double end)
{
    if (m_duration <= 0)
        return;
    double span = std::clamp(end - start, std::min(kMinSpan, m_duration), m_duration);
    double s = start;
    if (end - start < span) // 低于最小跨度时以中心扩展
        s = (start + end) / 2 - span / 2;
    s = std::clamp(s, 0.0, m_duration - span);
    m_viewStart = s;
    m_viewEnd = s + span;
    emit changed();
}

void TimelineState::zoomAround(double anchor, double factor)
{
    const double span = viewSpan();
    const double newSpan = std::clamp(span * factor, std::min(kMinSpan, m_duration), m_duration);
    const double ratio = span > 0 ? (anchor - m_viewStart) / span : 0.5;
    const double start = anchor - ratio * newSpan;
    setView(start, start + newSpan);
}

void TimelineState::panBy(double seconds)
{
    setView(m_viewStart + seconds, m_viewEnd + seconds);
}

void TimelineState::centerOn(double seconds)
{
    const double span = viewSpan();
    setView(seconds - span / 2, seconds + span / 2);
}

void TimelineState::fitRange(double start, double end)
{
    const double span = std::max(end - start, kMinSpan);
    const double pad = span * 0.05;
    setView(start - pad, end + pad);
}

void TimelineState::showAll()
{
    setView(0, m_duration);
}

void TimelineState::notifyManualPan()
{
    m_manualPan.restart();
}

void TimelineState::followPlayhead()
{
    if (m_manualPan.isValid() && m_manualPan.elapsed() < kFollowPauseMs)
        return;
    if (m_playhead < m_viewStart || m_playhead > m_viewEnd) {
        const double span = viewSpan();
        setView(m_playhead - span * 0.1, m_playhead + span * 0.9);
    }
}

int TimelineState::spanAt(double seconds) const
{
    // 后添加的片段画在上层，优先命中
    for (int i = int(m_spans.size()) - 1; i >= 0; --i) {
        const Span& s = m_spans.at(i);
        const double end = s.end.value_or(s.start);
        if (seconds >= s.start && seconds <= end)
            return i;
    }
    return -1;
}

double TimelineState::tickStep(double span)
{
    for (const double step : kTickSteps) {
        if (span / step <= 8)
            return step;
    }
    return kTickSteps.back();
}

} // namespace tsm
