#include "HourPieWidget.h"

#include "../bookshelf/DurationFormat.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "../settings/Theme.h"

namespace Genesis {

namespace {
const QColor kAccent("#33CCFF");      // ordinary hours
const QColor kRing("#E0E6ED");        // outer ring outline
const QColor kRingInner("#F0F4F8");   // inner ring outline (sector bases)

// Ranking colours for the top three hours, in rank order.
const QColor kRank1("#F84914");   // 1st
const QColor kRank2("#59187E");   // 2nd
const QColor kRank3("#2FB4AF");   // 3rd

// One sector per hour, 15 degrees each.
constexpr double kSectorDeg = 360.0 / 24.0;
// Label every 2 hours (0,2,4...22) - twelve labels around the dial. All 24
// would collide; 3-hour steps read sparser than the ring's own 24 divisions.
constexpr int kLabelStep = 2;
// Gap left between the outer ring and the label ring.
constexpr double kLabelGap = 14.0;
// Thickness of an empty hour's stub, so every hour keeps a visible slot on the
// ring even with no reading.
constexpr double kStubThickness = 4.0;
// Fraction of the inner..outer span a FULL-VALUE band may occupy. < 1 leaves a
// margin so the busiest hour stops short of the ring outline instead of
// touching (and visually breaking) it.
constexpr double kBandFill = 0.82;
} // namespace

HourPieWidget::HourPieWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(160);
    // Labels follow the shelf text theme; the dial's own colours do not.
    connect(&Theme::instance(), &Theme::changed, this,
            [this]() { update(); });
}

void HourPieWidget::setBuckets(const QHash<int, ReadingBucket>& buckets)
{
    m_buckets = buckets;
    update();
}

void HourPieWidget::setTodayBuckets(const QHash<int, ReadingBucket>& buckets)
{
    m_today = buckets;
    update();
}

void HourPieWidget::setMetric(Metric metric)
{
    if (metric == m_metric)
        return;
    m_metric = metric;
    update();
}

int HourPieWidget::hourValue(int hour) const
{
    const ReadingBucket b = m_buckets.value(hour);
    return m_metric == Metric::Seconds ? b.seconds : b.count;
}

QString HourPieWidget::hourText(int hour) const
{
    const ReadingBucket b = m_buckets.value(hour);
    if (m_metric == Metric::Seconds)
        return formatDuration(b.seconds);
    return QString::fromUtf8("%1 次").arg(b.count);
}

int HourPieWidget::totalValue() const
{
    int total = 0;
    for (int h = 0; h < 24; ++h)
        total += hourValue(h);
    return total;
}

int HourPieWidget::todayTotalValue() const
{
    int total = 0;
    for (int h = 0; h < 24; ++h) {
        const ReadingBucket b = m_today.value(h);
        total += m_metric == Metric::Seconds ? b.seconds : b.count;
    }
    return total;
}

QList<int> HourPieWidget::rankedHours() const
{
    QList<int> hours;
    for (int h = 0; h < 24; ++h)
        if (hourValue(h) > 0)
            hours << h;
    // Descending by value; a stable sort keeps clock order among ties.
    std::stable_sort(hours.begin(), hours.end(), [this](int a, int b) {
        return hourValue(a) > hourValue(b);
    });
    return hours;
}

void HourPieWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QList<int> ranked = rankedHours();
    if (ranked.isEmpty()) {
        p.setPen(Theme::instance().mutedText());
        p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("暂无阅读记录"));
        return;
    }

    // Dial on the left, ranking on the right. The label ring sits OUTSIDE the
    // dial, so the geometry budget must leave room for it on every side.
    const int legendW = qMin(150, width() / 2);
    const int availW = width() - legendW - 16;
    const double side = qMax(50.0, qMin(height() - 2.0 * (kLabelGap + 10),
                                         availW - 2.0 * kLabelGap));
    const QPointF c(8 + availW / 2.0, height() / 2.0);
    const double rOuter = side / 2.0;             // outline radius
    const double rInner = rOuter * 0.40;          // every sector starts here
    // A band at full value stops short of the outline, so the busiest hour
    // never collides with the ring and the chart keeps a clean edge.
    const double rMax = rInner + (rOuter - rInner) * kBandFill;

    int maxHour = 1;
    for (int h = 0; h < 24; ++h)
        maxHour = qMax(maxHour, hourValue(h));

    // Ring outlines: the inner circle the sectors grow from, and the outer
    // limit they can reach.
    p.setPen(QPen(kRing, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rOuter, rOuter);
    p.setPen(QPen(kRingInner, 1.0));
    p.drawEllipse(c, rInner, rInner);

    // --- 24 fixed annular sectors ---
    // Each spans exactly 15 degrees and is drawn as a RING BAND (an annulus),
    // not a wedge: the band's outer edge pushes away from the inner circle by
    // an amount proportional to the value, so the band's radial THICKNESS
    // encodes the reading time and its angle alone identifies the hour.
    const double half = kSectorDeg / 2.0;
    for (int h = 0; h < 24; ++h) {
        const int v = hourValue(h);

        const int rank = ranked.indexOf(h);   // 0-based position in the ranking
        QColor col = kAccent;
        if (rank == 0)
            col = kRank1;
        else if (rank == 1)
            col = kRank2;
        else if (rank == 2)
            col = kRank3;

        const double startDeg = h * kSectorDeg - 90.0;
        const double a0 = qDegreesToRadians(startDeg - half);
        const double a1 = qDegreesToRadians(startDeg + half);

        // An hour with no reading keeps a thin stub so the ring never shows a
        // gap - the hour still reads as a position on the clock.
        const double thickness =
            v > 0 ? (rMax - rInner) * (double(v) / maxHour) : kStubThickness;
        const double r1 = rInner;              // band's inner edge
        const double r2 = rInner + thickness;  // band's outer edge

        QPainterPath path;
        path.moveTo(c + QPointF(qCos(a0), qSin(a0)) * r1);
        path.lineTo(c + QPointF(qCos(a0), qSin(a0)) * r2);
        // Outer arc at r2, then back along the inner arc at r1.
        path.arcTo(QRectF(c.x() - r2, c.y() - r2, r2 * 2, r2 * 2),
                   -(startDeg - half), -kSectorDeg);
        path.lineTo(c + QPointF(qCos(a1), qSin(a1)) * r1);
        path.arcTo(QRectF(c.x() - r1, c.y() - r1, r1 * 2, r1 * 2),
                   -(startDeg + half), kSectorDeg);
        path.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawPath(path);
    }

    // --- hour labels, OUTSIDE the dial ---
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.75);
    p.setFont(small);
    const QFontMetrics fm(small);
    for (int h = 0; h < 24; h += kLabelStep) {
        const double rad = qDegreesToRadians(h * kSectorDeg - 90.0);
        const QPointF dir(qCos(rad), qSin(rad));
        const QPointF at = c + dir * (rOuter + kLabelGap);
        const QRect box(int(at.x()) - 14, int(at.y()) - fm.height() / 2, 28,
                        fm.height());
        p.setPen(Theme::instance().mutedText());
        p.drawText(box, Qt::AlignCenter, QString::number(h));
    }

    // --- centre readout: TODAY's total ---
    const int today = todayTotalValue();
    const QString centreText =
        m_metric == Metric::Seconds
            ? formatDuration(today)
            : QString::fromUtf8("%1 次").arg(today);
    QFont centreFont = font();
    const double fitW = rInner * 1.7;
    while (centreFont.pointSizeF() > 5.0
           && QFontMetricsF(centreFont).horizontalAdvance(centreText) > fitW)
        centreFont.setPointSizeF(centreFont.pointSizeF() - 0.5);
    p.setFont(centreFont);
    p.setPen(Theme::instance().text());
    const QFontMetrics cfm(centreFont);
    const QFontMetrics sfm(small);
    p.drawText(QRect(int(c.x() - rInner), int(c.y() - cfm.height()),
                     int(rInner * 2), cfm.height()),
               Qt::AlignCenter, centreText);
    p.setFont(small);
    p.setPen(Theme::instance().mutedText());
    p.drawText(QRect(int(c.x() - rInner), int(c.y()),
                     int(rInner * 2), sfm.height()),
               Qt::AlignCenter, QString::fromUtf8("今日"));
    p.setFont(font());

    // --- ranking, top 3 highlighted ---
    const int legendX = int(c.x() + rOuter + kLabelGap + 12);
    if (legendX < width() - 40) {
        const QFontMetrics lm(font());
        const int rowH = lm.height() + 3;
        const int shown = qMin(3, ranked.size());
        int y = height() / 2 - (rowH * shown) / 2;

        // Header
        p.setPen(Theme::instance().mutedText());
        p.drawText(QRect(legendX, y - lm.height() - 2, width() - legendX - 4,
                         lm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8("阅读排行"));

        static const QColor kRank[3] = {kRank1, kRank2, kRank3};
        for (int i = 0; i < shown; ++i) {
            const int h = ranked.at(i);
            // Colour chip matching the sector, so the legend ties back to the
            // ring.
            p.setPen(Qt::NoPen);
            p.setBrush(kRank[i]);
            p.drawRoundedRect(QRect(legendX, y + (rowH - 9) / 2, 9, 9), 2, 2);

            p.setPen(Theme::instance().text());
            p.drawText(QRect(legendX + 14, y, width() - legendX - 18, rowH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromUtf8("%1–%2 时")
                           .arg(QString::number(h).rightJustified(
                               2, QLatin1Char('0')))
                           .arg(QString::number((h + 1) % 24).rightJustified(
                               2, QLatin1Char('0'))));
            p.setPen(Theme::instance().mutedText());
            p.drawText(QRect(0, y, width() - 6, rowH),
                       Qt::AlignRight | Qt::AlignVCenter, hourText(h));
            y += rowH;
        }
    }
}

} // namespace Genesis
