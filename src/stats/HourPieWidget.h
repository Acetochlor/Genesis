#ifndef HOURPIEWIDGET_H
#define HOURPIEWIDGET_H

#include "../library/ReadingLogStore.h"

#include <QHash>
#include <QWidget>

namespace Genesis {

// The "when do you read" chart, drawn as a 24-hour TIME WHEEL: a ring divided
// into 24 fixed 15-degree sectors, hour 0 at the top running clockwise. Each
// sector grows OUTWARD from the ring's inner radius, so its AREA encodes how
// much was read in that hour while the angle alone identifies the hour.
//
// Fixed angles rather than a true pie: an angle-proportional pie of 24 hours
// collapses the quiet hours into unreadable slivers, and it cannot show "which
// hour" as a position. With a fixed ring every hour keeps a permanent place on
// the clock and the shape still reads at a glance.
class HourPieWidget : public QWidget {
    Q_OBJECT

public:
    // Which figure each sector is weighted by.
    enum class Metric { Seconds, Count };

    explicit HourPieWidget(QWidget* parent = nullptr);

    // hour of day (0..23) -> bucket, as ReadingLogStore produces. Drives the
    // ring and the ranking.
    void setBuckets(const QHash<int, ReadingBucket>& buckets);
    // Today's buckets, shown as the dial centre's readout. Kept separate from
    // setBuckets() because the ring follows the panel's 月/年/总 range while
    // the centre is always "today".
    void setTodayBuckets(const QHash<int, ReadingBucket>& buckets);
    void setMetric(Metric metric);
    Metric metric() const { return m_metric; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Value of one hour under the current metric.
    int hourValue(int hour) const;
    // Human string for one hour under the current metric.
    QString hourText(int hour) const;
    // Total across all 24 hours under the current metric.
    int totalValue() const;
    // Total of the "today" buckets under the current metric.
    int todayTotalValue() const;
    // Hours 0..23 ordered by value, descending; ties keep clock order.
    QList<int> rankedHours() const;

    QHash<int, ReadingBucket> m_buckets;
    QHash<int, ReadingBucket> m_today;
    Metric m_metric = Metric::Seconds;
};

} // namespace Genesis

#endif // HOURPIEWIDGET_H
