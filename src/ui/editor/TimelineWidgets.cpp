#include "ui/editor/TimelineWidgets.h"

#include "core/Format.h"
#include "core/TimelineState.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>
#include <cmath>

namespace tsm {

namespace {
constexpr double kSideMargin = 8;
constexpr double kLabelHeight = 16;
constexpr double kFooterHeight = 16;

const QColor kTrackBg(0x1b, 0x1e, 0x25);
const QColor kTickColor(0x3a, 0x40, 0x4d);
const QColor kLabelColor(0x8b, 0x93, 0xa7);
const QColor kPlayedColor(42, 171, 238, 38);
const QColor kPlayhead(0xff, 0x5f, 0x6d);
const QColor kHover(255, 255, 255, 90);

QString tickLabel(double t, double step)
{
    // 刻度小于 1 秒时显示毫秒
    if (step < 1)
        return formatTimecode(t);
    const int total = int(std::lround(t));
    if (total >= 3600)
        return QStringLiteral("%1:%2:%3").arg(total / 3600).arg((total / 60) % 60, 2, 10, QChar(u'0')).arg(total % 60, 2, 10, QChar(u'0'));
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QChar(u'0')).arg(total % 60, 2, 10, QChar(u'0'));
}

double wheelSteps(const QWheelEvent* e)
{
    return e->angleDelta().y() / 120.0;
}
} // namespace

QColor clipColor(int index)
{
    static const QColor palette[] = {QColor(0x2a, 0xab, 0xee), QColor(0xf5, 0xa6, 0x23), QColor(0x3e, 0xcf, 0x8e),
                                     QColor(0xc0, 0x7c, 0xff), QColor(0xff, 0x7a, 0x90), QColor(0x4f, 0xd1, 0xd9)};
    return palette[std::abs(index) % 6];
}

// ---------------------------------------------------------------- TimelineTrack

TimelineTrack::TimelineTrack(TimelineState* state, QWidget* parent)
    : QWidget(parent)
    , m_state(state)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(78);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_state, &TimelineState::changed, this, qOverload<>(&QWidget::update));
}

QRectF TimelineTrack::trackRect() const
{
    return QRectF(kSideMargin, kLabelHeight, width() - 2 * kSideMargin, height() - kLabelHeight - kFooterHeight);
}

double TimelineTrack::timeAt(double x) const
{
    const QRectF r = trackRect();
    const double ratio = r.width() > 0 ? (x - r.left()) / r.width() : 0;
    return std::clamp(m_state->viewStart() + ratio * m_state->viewSpan(), 0.0, m_state->duration());
}

double TimelineTrack::xAt(double seconds) const
{
    const QRectF r = trackRect();
    const double span = m_state->viewSpan();
    return r.left() + (span > 0 ? (seconds - m_state->viewStart()) / span * r.width() : 0);
}

void TimelineTrack::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const double vs = m_state->viewStart();
    const double ve = m_state->viewEnd();

    QPainterPath bg;
    bg.addRoundedRect(track, 6, 6);
    p.fillPath(bg, kTrackBg);
    p.save();
    p.setClipPath(bg);

    // 已播放部分高亮
    const double playX = xAt(m_state->playhead());
    if (playX > track.left())
        p.fillRect(QRectF(track.left(), track.top(), std::min(playX, track.right()) - track.left(), track.height()), kPlayedColor);

    // 刻度
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    const double step = TimelineState::tickStep(m_state->viewSpan());
    const double first = std::ceil(vs / step) * step;
    p.setPen(QPen(kTickColor, 1));
    for (double t = first; t <= ve + 1e-9; t += step) {
        const double x = xAt(t);
        p.drawLine(QPointF(x, track.top()), QPointF(x, track.top() + 6));
    }

    // 片段色块
    const auto& spans = m_state->spans();
    for (int i = 0; i < spans.size(); ++i) {
        const auto& s = spans.at(i);
        const QColor color = clipColor(i);
        const bool selected = i == m_state->selected();
        const double x0 = xAt(s.start);
        if (s.end) {
            const QRectF block(x0, track.top() + 10, std::max(2.0, xAt(*s.end) - x0), track.height() - 18);
            QColor fill = color;
            fill.setAlpha(selected ? 150 : 80);
            p.setPen(selected ? QPen(color.lighter(130), 2) : QPen(color, 1));
            p.setBrush(fill);
            p.drawRoundedRect(block, 4, 4);
            if (block.width() > 24) {
                p.setPen(Qt::white);
                p.setFont(small);
                const QString label = QString::number(i + 1) + (s.emoji.isEmpty() ? QString() : QStringLiteral(" ") + s.emoji);
                p.drawText(block.adjusted(5, 0, -3, 0), Qt::AlignLeft | Qt::AlignVCenter, label);
            }
        } else {
            // 未设终点：起点处画一面小旗
            p.setPen(QPen(color, selected ? 2.5 : 1.5));
            p.drawLine(QPointF(x0, track.top() + 6), QPointF(x0, track.bottom() - 4));
            QPolygonF flag;
            flag << QPointF(x0, track.top() + 6) << QPointF(x0 + 9, track.top() + 10) << QPointF(x0, track.top() + 14);
            p.setBrush(color);
            p.drawPolygon(flag);
        }
    }

    // 悬停竖线
    if (const auto hover = m_state->hover()) {
        p.setPen(QPen(kHover, 1, Qt::DashLine));
        const double x = xAt(*hover);
        p.drawLine(QPointF(x, track.top()), QPointF(x, track.bottom()));
    }
    p.restore();

    // 播放头
    if (playX >= track.left() - 1 && playX <= track.right() + 1) {
        p.setPen(QPen(kPlayhead, 2));
        p.drawLine(QPointF(playX, track.top() - 2), QPointF(playX, track.bottom()));
        QPolygonF head;
        head << QPointF(playX - 5, track.top() - 6) << QPointF(playX + 5, track.top() - 6) << QPointF(playX, track.top() + 1);
        p.setBrush(kPlayhead);
        p.setPen(Qt::NoPen);
        p.drawPolygon(head);
    }

    // 刻度标签
    p.setFont(small);
    p.setPen(kLabelColor);
    const QFontMetricsF fm(small);
    for (double t = first; t <= ve + 1e-9; t += step) {
        const QString label = tickLabel(t, step);
        const double w = fm.horizontalAdvance(label) + 4;
        // 两端的标签收进控件内，避免被裁掉
        const double left = std::clamp(xAt(t) - w / 2, 0.0, width() - w);
        p.drawText(QRectF(left, 0, w, kLabelHeight - 2), Qt::AlignCenter, label);
    }

    // 底部：可视起点 / 操作提示 / 可视终点
    const QRectF footer(track.left(), track.bottom() + 1, track.width(), kFooterHeight - 1);
    p.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter, formatTimecode(vs));
    p.drawText(footer, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("滚轮缩放 · Shift 平移"));
    p.drawText(footer, Qt::AlignRight | Qt::AlignVCenter, formatTimecode(ve));
}

void TimelineTrack::mousePressEvent(QMouseEvent* e)
{
    m_lastX = e->position().x();
    const bool pan = e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier));
    if (pan) {
        m_drag = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    m_drag = Drag::Scrub;
    const double t = timeAt(e->position().x());
    // 按下的位置落在某个片段上时同时选中它
    if (const int index = m_state->spanAt(t); index >= 0)
        emit clipPressed(index);
    emit scrubbed(t);
}

void TimelineTrack::mouseMoveEvent(QMouseEvent* e)
{
    const double x = e->position().x();
    const double t = timeAt(x);
    m_state->setHover(t);
    if (m_drag == Drag::Scrub) {
        emit scrubbed(t);
    } else if (m_drag == Drag::Pan) {
        const double dt = -(x - m_lastX) / std::max(1.0, trackRect().width()) * m_state->viewSpan();
        m_lastX = x;
        m_state->panBy(dt);
        m_state->notifyManualPan();
    } else {
        const int index = m_state->spanAt(t);
        if (index >= 0) {
            const auto& s = m_state->spans().at(index);
            const QString end = s.end ? formatTimecode(*s.end) : QStringLiteral("未设终点");
            QToolTip::showText(e->globalPosition().toPoint(),
                               QStringLiteral("片段 %1: %2 – %3").arg(index + 1).arg(formatTimecode(s.start), end), this);
        } else {
            QToolTip::hideText();
        }
    }
}

void TimelineTrack::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == Drag::Scrub)
        emit scrubFinished();
    m_drag = Drag::None;
    unsetCursor();
}

void TimelineTrack::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int index = m_state->spanAt(timeAt(e->position().x()));
    if (index >= 0) {
        const auto& s = m_state->spans().at(index);
        m_state->fitRange(s.start, s.end.value_or(s.start + 1));
    } else {
        m_state->showAll();
    }
    m_state->notifyManualPan();
}

void TimelineTrack::wheelEvent(QWheelEvent* e)
{
    const bool horizontal = std::abs(e->angleDelta().x()) > std::abs(e->angleDelta().y());
    if (horizontal || (e->modifiers() & Qt::ShiftModifier)) {
        const double delta = horizontal ? e->angleDelta().x() : e->angleDelta().y();
        m_state->panBy(-delta / 120.0 * m_state->viewSpan() * 0.1);
        m_state->notifyManualPan();
    } else {
        m_state->zoomAround(timeAt(e->position().x()), std::pow(0.8, wheelSteps(e)));
        m_state->notifyManualPan();
    }
    e->accept();
}

void TimelineTrack::leaveEvent(QEvent*)
{
    m_state->setHover(std::nullopt);
}

// ---------------------------------------------------------------- TimelineOverview

TimelineOverview::TimelineOverview(TimelineState* state, QWidget* parent)
    : QWidget(parent)
    , m_state(state)
{
    setFixedHeight(20);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    connect(m_state, &TimelineState::changed, this, qOverload<>(&QWidget::update));
}

double TimelineOverview::timeAt(double x) const
{
    const double w = width() - 2 * kSideMargin;
    return std::clamp((x - kSideMargin) / std::max(1.0, w) * m_state->duration(), 0.0, m_state->duration());
}

double TimelineOverview::xAt(double seconds) const
{
    const double d = m_state->duration();
    return kSideMargin + (d > 0 ? seconds / d : 0) * (width() - 2 * kSideMargin);
}

void TimelineOverview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar(kSideMargin, 4, width() - 2 * kSideMargin, height() - 8);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x22, 0x26, 0x2e));
    p.drawRoundedRect(bar, 3, 3);

    const auto& spans = m_state->spans();
    for (int i = 0; i < spans.size(); ++i) {
        const auto& s = spans.at(i);
        QColor c = clipColor(i);
        c.setAlpha(i == m_state->selected() ? 230 : 150);
        const double x0 = xAt(s.start);
        const double x1 = s.end ? xAt(*s.end) : x0 + 2;
        p.setBrush(c);
        p.drawRect(QRectF(x0, bar.top(), std::max(2.0, x1 - x0), bar.height()));
    }

    const QRectF view(xAt(m_state->viewStart()), 1, std::max(4.0, xAt(m_state->viewEnd()) - xAt(m_state->viewStart())), height() - 2);
    p.setBrush(QColor(255, 255, 255, 25));
    p.setPen(QPen(QColor(255, 255, 255, 160), 1));
    p.drawRoundedRect(view, 3, 3);

    p.setPen(QPen(kPlayhead, 2));
    const double px = xAt(m_state->playhead());
    p.drawLine(QPointF(px, 1), QPointF(px, height() - 1));
}

void TimelineOverview::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const double x = e->position().x();
    const bool insideView = x >= xAt(m_state->viewStart()) && x <= xAt(m_state->viewEnd());
    if (!insideView)
        m_state->centerOn(timeAt(x));
    m_state->notifyManualPan();
    m_dragging = true;
    m_lastX = x;
}

void TimelineOverview::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    const double x = e->position().x();
    const double dt = (x - m_lastX) / std::max(1.0, double(width() - 2 * kSideMargin)) * m_state->duration();
    m_lastX = x;
    m_state->panBy(dt);
    m_state->notifyManualPan();
}

void TimelineOverview::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

} // namespace tsm
