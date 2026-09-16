#ifndef TRENDCHARTWIDGET_H
#define TRENDCHARTWIDGET_H

#include <QDate>
#include <QList>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <functional>

class QScrollBar;

namespace Genesis {

// The reading-trend curve: minutes read per bucket, with the bucket chosen by
// the caller (a day for the month view, a month for the year/all view).
//
// Owns its own horizontal QScrollBar instead of living inside a QScrollArea,
// because the Y axis must stay pinned on the left while the plot scrolls; with
// a QScrollArea the axis would either scroll away with the content or need a
// second frozen widget kept in sync.
class TrendChartWidget : public QWidget {
    Q_OBJECT

public:
    explicit TrendChartWidget(QWidget* parent = nullptr);

    // One plotted point. `label` is what the x axis shows under it.
    struct Point {
        QDate date;       // bucket identity (first day of the month for a
                          // monthly bucket)
        QString label;    // axis label, already formatted
        int seconds = 0;  // reading time in the bucket
    };

    // Replace the data. Points with fewer than a minute are dropped, matching
    // the panel's "under 1 minute does not count" rule.
    void setPoints(const QList<Point>& points);
    void clearPoints();

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    // The chart scrolls its series itself: a wheel gesture pans it, and a
    // left-drag pans it like a map. Neither is handled by a plain QWidget, and
    // the thin scrollbar alone was an impractical target in this narrow column.
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // Left gutter reserved for the y-axis labels (never scrolls).
    int axisWidth() const;
    // Plot area inside the widget, excluding the gutter and bottom labels.
    QRect plotRect() const;
    void updateScrollBar();
    // Park the series at its right edge, so the newest date is on screen.
    void pinToNewest();
    // Interpolate the data points into a smooth polyline (Catmull-Rom), using
    // `pointPos` to place each sample. Shared by the fill and the stroke.
    QList<QPointF> smoothCurve(
        const std::function<QPointF(int, int)>& pointPos) const;

    QList<Point> m_points;
    QScrollBar* m_scroll = nullptr;
    int m_yMaxMinutes = 60;   // axis top, in minutes
    // Drag-to-pan state; a null origin means no drag is in progress.
    QPoint m_dragOrigin;
    int m_dragStartValue = 0;
    // True once the user has moved the series themselves. Until then the view
    // re-parks on the newest date whenever the chart is re-laid out, so a late
    // first resize cannot leave it stranded on the oldest data.
    bool m_userScrolled = false;
};

} // namespace Genesis

#endif // TRENDCHARTWIDGET_H
