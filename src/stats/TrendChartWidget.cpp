#include "TrendChartWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>

#include "../settings/Theme.h"

namespace Genesis {

namespace {
const QColor kAccent("#33CCFF");
const QColor kFill(51, 204, 255, 40);
const QColor kGrid("#E0E6ED");
const QColor kAxis("#B9C2CC");   // axis rules - darker than the gridlines

constexpr int kTopPad = 10;
constexpr int kBottomPad = 22;   // room for the x labels
constexpr int kAxisGap = 6;
// Horizontal slot per data point. Sized so a full "9月15日" label fits under
// its own point without touching its neighbours.
constexpr int kPxPerPoint = 56;
constexpr int kSidePad = 10;
constexpr int kMinAxisWidth = 34;

// Y-axis duration label: "0分" / "45分" below an hour, "1时30分" at or above
// one. Compact enough to fit the gutter at four divisions.
QString durationLabel(int minutes)
{
    if (minutes < 60)
        return QString::fromUtf8("%1分").arg(minutes);
    const int h = minutes / 60;
    const int m = minutes % 60;
    return m == 0 ? QString::fromUtf8("%1时").arg(h)
                  : QString::fromUtf8("%1时%2分").arg(h).arg(m);
}
} // namespace

TrendChartWidget::TrendChartWidget(QWidget* parent)
    : QWidget(parent)
{
    m_scroll = new QScrollBar(Qt::Horizontal, this);
    m_scroll->setVisible(false);
    // A default scrollbar is only ~14px tall, which is a hard target with a
    // mouse in this narrow column - give it some height back to grab.
    m_scroll->setFixedHeight(18);
    connect(m_scroll, &QScrollBar::valueChanged, this,
            [this](int) { update(); });
    // Dragging the bar itself is a user scroll; valueChanged alone cannot tell
    // a user move from our own re-parking, so listen for the user-only signal.
    connect(m_scroll, &QScrollBar::sliderPressed, this,
            [this]() { m_userScrolled = true; });
    setMinimumHeight(120);
    // The chart accepts wheel/drag itself (see wheelEvent / mouse events), so
    // it must receive those events rather than letting them pass through.
    setFocusPolicy(Qt::NoFocus);
    // Axis and label text follows the shelf text theme; the curve does not.
    connect(&Theme::instance(), &Theme::changed, this,
            [this]() { update(); });
}

void TrendChartWidget::wheelEvent(QWheelEvent* event)
{
    // A horizontal wheel/trackpad gesture scrolls the series; a plain vertical
    // wheel does too, because this chart is the only horizontal surface and the
    // user's intent over it is unambiguous. At either end the event is ignored
    // so an outer scroll area can take over.
    if (!m_scroll->isVisible()) {
        event->ignore();
        return;
    }
    const int d = event->angleDelta().x() != 0 ? event->angleDelta().x()
                                               : event->angleDelta().y();
    if (d == 0) {
        event->ignore();
        return;
    }
    const int before = m_scroll->value();
    // Scrolling "down"/"right" (negative delta) moves toward the newest data.
    m_scroll->setValue(before - d);
    if (m_scroll->value() == before) {
        event->ignore();   // already at the end: let the outer area scroll
        return;
    }
    m_userScrolled = true;
    event->accept();
}

void TrendChartWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_scroll->isVisible() && event->button() == Qt::LeftButton) {
        m_dragOrigin = event->pos();
        m_dragStartValue = m_scroll->value();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TrendChartWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragOrigin.isNull() || !m_scroll->isVisible()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    // Drag the series sideways, the way a map or photo pans.
    const int dx = event->pos().x() - m_dragOrigin.x();
    m_scroll->setValue(m_dragStartValue - dx);
    m_userScrolled = true;
    event->accept();
}

void TrendChartWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragOrigin.isNull()) {
        m_dragOrigin = QPoint();
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

QSize TrendChartWidget::minimumSizeHint() const
{
    return QSize(120, 120);
}

int TrendChartWidget::axisWidth() const
{
    // Wide enough for the WIDEST label any division can produce, not just the
    // top one. The format changes shape across the scale ("45分" vs "1时45分"),
    // so the longest is usually a middle division - measuring only the top
    // label clipped the longer ones.
    const QFontMetrics fm(font());
    int widest = 0;
    for (int i = 0; i <= 4; ++i)
        widest = qMax(widest,
                      fm.horizontalAdvance(
                          durationLabel(int(double(i) / 4 * m_yMaxMinutes))));
    return qMax(kMinAxisWidth, widest + kAxisGap);
}

QRect TrendChartWidget::plotRect() const
{
    const int bottom = height() - kBottomPad
                       - (m_scroll->isVisible() ? m_scroll->sizeHint().height()
                                                : 0);
    return QRect(axisWidth(), kTopPad,
                 qMax(1, width() - axisWidth() - kSidePad),
                 qMax(1, bottom - kTopPad));
}

void TrendChartWidget::setPoints(const QList<Point>& points)
{
    m_points.clear();
    int maxMinutes = 0;
    for (const Point& p : points) {
        // The panel's rule: anything under a minute is not reading.
        if (p.seconds < 60)
            continue;
        m_points << p;
        maxMinutes = qMax(maxMinutes, p.seconds / 60);
    }

    // Axis top: 15% headroom, then rounded up to a readable step so the
    // gridline labels are round numbers. Floored at 10 so an almost-empty
    // chart still has a sensible scale.
    const double want = qMax(10.0, maxMinutes * 1.15);
    static const double kSteps[] = {10, 15, 20, 30, 45, 60, 90, 120,
                                    180, 240, 300, 480, 600, 900, 1200};
    m_yMaxMinutes = int(kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1]);
    for (double s : kSteps) {
        if (s >= want) {
            m_yMaxMinutes = int(s);
            break;
        }
    }

    updateScrollBar();
    // The newest date sits at the RIGHT edge: park the scroll at its maximum so
    // the most recent data is what the user sees on open. Fresh data also
    // clears the "user has scrolled" flag, so a later resize re-parks.
    m_userScrolled = false;
    pinToNewest();
    update();
}

void TrendChartWidget::pinToNewest()
{
    // Park at the right end whenever there is range to move through. NOT
    // gated on m_scroll->isVisible(): setPoints() runs while the panel is
    // still being constructed, and a child widget reports isVisible() == false
    // until its whole parent chain is shown - which made this a no-op on the
    // very first load, leaving the chart parked on the OLDEST dates.
    if (m_scroll->maximum() > m_scroll->minimum())
        m_scroll->setValue(m_scroll->maximum());
}

void TrendChartWidget::clearPoints()
{
    m_points.clear();
    updateScrollBar();
    update();
}

void TrendChartWidget::updateScrollBar()
{
    const int contentW = m_points.size() * kPxPerPoint + 2 * kSidePad;
    const int viewportW = plotRect().width();
    const bool needed = contentW > viewportW && !m_points.isEmpty();
    m_scroll->setVisible(needed);
    if (needed) {
        m_scroll->setGeometry(axisWidth(),
                              height() - m_scroll->sizeHint().height(),
                              width() - axisWidth() - kSidePad,
                              m_scroll->sizeHint().height());
        m_scroll->setRange(0, contentW - viewportW);
        m_scroll->setPageStep(viewportW);
        m_scroll->setSingleStep(kPxPerPoint);
    } else {
        m_scroll->setRange(0, 0);
    }
}

void TrendChartWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // The first real size arrives AFTER setPoints() ran (during the panel's
    // construction the widget was still at its default geometry, so the scroll
    // range computed then was wrong). Recompute the range and, while the user
    // has not positioned the series themselves, keep it parked on the newest
    // date. Once they HAVE scrolled, leave their position alone.
    const bool wasAtEnd = !m_userScrolled;
    updateScrollBar();
    if (wasAtEnd)
        pinToNewest();
}

QList<QPointF> TrendChartWidget::smoothCurve(
    const std::function<QPointF(int, int)>& pointPos) const
{
    // Sample the data points, then run a Catmull-Rom spline through them: each
    // segment is drawn as a cubic whose control points come from the
    // neighbouring samples, giving a smooth join at every point while still
    // PASSING THROUGH each data value (unlike a Bezier fit, which would drift
    // off the actual readings).
    QList<QPointF> pts;
    pts.reserve(m_points.size());
    for (int i = 0; i < m_points.size(); ++i)
        pts << pointPos(i, m_points[i].seconds / 60);

    if (pts.size() < 3)
        return pts;   // nothing to smooth

    const int kSub = 12;   // samples per segment
    QList<QPointF> out;
    out.reserve((pts.size() - 1) * kSub + 1);
    // Duplicate the ends so the first and last segments curve the same way as
    // the interior ones instead of starting with a straight run.
    auto at = [&](int i) {
        return pts.at(qBound(0, i, pts.size() - 1));
    };
    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF p0 = at(i - 1), p1 = at(i);
        const QPointF p2 = at(i + 1), p3 = at(i + 2);
        for (int s = 0; s < kSub; ++s) {
            const double t = double(s) / kSub;
            const double t2 = t * t, t3 = t2 * t;
            // Catmull-Rom basis, tension 0.5.
            const QPointF v = 0.5
                * ((2.0 * p1)
                   + (-p0 + p2) * t
                   + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                   + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
            out << v;
        }
    }
    out << pts.last();
    return out;
}

void TrendChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect plot = plotRect();
    if (plot.width() <= 2 || plot.height() <= 2)
        return;

    // --- gridlines: dashed, so they read as guides behind the curve ---
    // The horizontal rules are drawn later, once the x positions are known, so
    // every vertical rule can align with an actual data point.
    p.setFont(font());
    const QFontMetrics fm(font());
    QPen dash(kGrid, 1.0, Qt::DashLine);
    dash.setDashPattern({3, 3});
    const int divs = 4;
    for (int i = 0; i <= divs; ++i) {
        const double frac = double(i) / divs;
        const int y = plot.bottom() - int(frac * plot.height());
        p.setPen(dash);
        p.drawLine(plot.left(), y, plot.right(), y);
        p.setPen(Theme::instance().mutedText());
        p.drawText(QRect(0, y - fm.height() / 2, axisWidth() - kAxisGap,
                         fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   durationLabel(int(frac * m_yMaxMinutes)));
    }

    if (m_points.isEmpty()) {
        p.setPen(Theme::instance().mutedText());
        p.drawText(plot, Qt::AlignCenter, QString::fromUtf8("暂无阅读记录"));
        return;
    }

    const int xOff = m_scroll->isVisible() ? m_scroll->value() : 0;
    auto pointPos = [&](int i, int minutes) {
        // Centre each point in its own kPxPerPoint slot.
        const double cx = plot.left() + kSidePad + i * kPxPerPoint
                          + kPxPerPoint / 2.0 - xOff;
        const double frac = qBound(0.0, double(minutes) / m_yMaxMinutes, 1.0);
        const double cy = plot.bottom() - frac * plot.height();
        return QPointF(cx, cy);
    };

    // --- vertical dashed rule under EVERY data point ---
    // One guide per date, running the full plot height, so a point's height can
    // be read straight against the y scale and its date located on the axis.
    // Clipped to the plot so a rule for an off-screen date cannot leak past the
    // y axis.
    {
        QPen vr(kGrid, 1.0, Qt::DashLine);
        vr.setDashPattern({3, 3});
        p.setPen(vr);
        p.setClipRect(plot);
        for (int i = 0; i < m_points.size(); ++i) {
            const double x = pointPos(i, 0).x();
            if (x < plot.left() || x > plot.right())
                continue;
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        p.setClipping(false);
    }

    // --- the left y axis: SOLID, the zero line ---
    p.setPen(QPen(kAxis, 1.0));
    p.drawLine(plot.left(), plot.top(), plot.left(), plot.bottom());
    // --- the bottom x axis: SOLID too, closing the plot ---
    p.drawLine(plot.left(), plot.bottom(), plot.right(), plot.bottom());

    // Samples a smooth curve is built through: the data points, interpolated
    // with a Catmull-Rom spline so the series reads as a flowing line rather
    // than a chain of straight segments. Shared by the fill and the stroke so
    // the two can never disagree.
    const QList<QPointF> curve = smoothCurve(pointPos);

    // --- filled area under the curve ---
    QPainterPath area;
    area.moveTo(curve.first().x(), plot.bottom());
    for (const QPointF& pt : curve)
        area.lineTo(pt);
    area.lineTo(curve.last().x(), plot.bottom());
    area.closeSubpath();
    p.setClipRect(plot);
    p.setPen(Qt::NoPen);
    p.setBrush(kFill);
    p.drawPath(area);
    p.setClipping(false);

    // --- the curve ---
    QPen line(kAccent, 2.0);
    line.setJoinStyle(Qt::RoundJoin);
    line.setCapStyle(Qt::RoundCap);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(curve.first());
    for (int i = 1; i < curve.size(); ++i)
        path.lineTo(curve.at(i));
    p.setClipRect(plot);
    p.drawPath(path);
    p.setClipping(false);

    // --- dots + x labels ---
    // The curve was clipped to the plot rect; the labels below it must NOT be,
    // or they vanish (they are drawn past plot.bottom()). Release the clip
    // first, and only clip for the dots themselves, which sit on the edge.
    p.setClipping(false);
    // Labels are full dates ("9月5日" / "2026年9月"). A point slot is wide
    // enough for one, so every date gets its own label; the stride is kept only
    // as a guard for a very narrow pane, where the slot shrinks below the text.
    const QFontMetrics xfm(font());
    int widest = 1;
    for (const Point& pt : std::as_const(m_points))
        widest = qMax(widest, xfm.horizontalAdvance(pt.label));
    const int stride = qMax(1, int(qCeil((widest + 10.0) / kPxPerPoint)));
    // Only the dots INSIDE the plot, clipped to it. The old test widened the
    // bounds by a whole point slot, so a point scrolled off to the left still
    // drew over the y axis and its date appeared beside the axis labels.
    p.setClipRect(plot);
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF pt = pointPos(i, m_points[i].seconds / 60);
        if (pt.x() < plot.left() || pt.x() > plot.right())
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        p.drawEllipse(pt, 3.0, 3.0);
    }
    p.setClipping(false);
    // Labels last, unclipped by the plot (they sit below it), but the SAME
    // in-plot test as the dots: a label whose point has scrolled off must not
    // be drawn, or its date appears floating beside the y-axis labels.
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF pt = pointPos(i, m_points[i].seconds / 60);
        if (i % stride != 0)
            continue;
        if (pt.x() < plot.left() || pt.x() > plot.right())
            continue;
        p.setPen(Theme::instance().mutedText());
        p.drawText(QRect(int(pt.x()) - widest / 2 - 2, plot.bottom() + 3,
                         widest + 4, kBottomPad - 4),
                   Qt::AlignHCenter | Qt::AlignTop, m_points[i].label);
    }
}

} // namespace Genesis
